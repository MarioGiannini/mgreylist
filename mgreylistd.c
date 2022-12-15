#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <error.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/stat.h>
 
#define GROWBY 256
#define VERSION "0.1.0"
/*
Copyright 2009 Mario Giannini

This file is part of mgreylist.
mgreylist is free software: you can redistribute it and/or modify it under 
the terms of the GNU General Public License as published by the Free Software 
Foundation, either version 3 of the License, or (at your option) any later version.

mgreylist is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; 
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with mgreylist. 
If not, see http://www.gnu.org/licenses/.
*/

// Node definition and support
typedef struct {
	time_t Reference;
	char* Triplet;
} Node;

typedef struct {
	char*Name;	
	Node* Nodes;
	int Count, Max;
} NodeList;


// Config settings
int RetryMin=600;	// Min time before item can be retried, in seconds
int RetryMax=86400;	// How long an item can live in the greylist. 24hour default
int Expire=5184000; // How long an item can list in the whitelist. 60 day default

char SocketFile[256] = "/var/run/mgreylistd/socket"; 	// Where to open socket
char LockFile[256] = "/var/run/mgreylistd/pid"; 	// Where to open lock file
char DBPath[256] = "/var/lib/mgreylistd";			// Where to store data files
char LogFile[256] = "/var/log/mgreylistd.log";		// Log file
char ConfigFile[256] = "/etc/mgreylistd/config";		// Config file
int fileMode = 0666;	// What to chmod socket to
int Update = 600; 		// How long between auto file saves

// Globals
int LastPurge=0;	// Time of last purge.  No need to purge more often than RetryMax
volatile int Terminate=0;	// Stop processing
char Emergency[1024];
time_t NextSave;
int IsDaemon=0;
int IsDiag = 0; // Set to 1 for full diagnostic logging

// Response strings back to caller
char respFALSE[]="false";
char respTRUE[]="true";
char respGREY[]="grey";
char respBLACK[]="black";
char respWHITE[]="white";
char respNONE[]="none";
char respERROR[]="error";
char respBYE[]="bye";

// Command strings for processing
char cmdADD[]="add";
char cmdDELETE[]="delete";
char cmdCHECK[]="check";
char cmdUPDATE[]="update";
char cmdSTATS[]="stats";
char cmdMRTG[]="mrtg";
char cmdLIST[]="list";
char cmdSAVE[]="save";
char cmdRELOAD[]="reload";
char cmdCLEAR[]="clear";

// List names
char listGREY[]="--grey";
char listWHITE[]="--white";
char listBLACK[]="--black";

NodeList GreyList = { "Grey", NULL, 0, 0};
NodeList WhiteList = { "White", NULL, 0, 0};
NodeList BlackList = { "Black", NULL, 0, 0};

// Helpers
time_t strToTimeT( char* Src)
{
	if (sizeof(time_t) == sizeof(int))
		return (time_t)atoi(Src);
	else if (sizeof(time_t) == sizeof(long))
		return (time_t)atol(Src);
	else if (sizeof(time_t) == sizeof(long long))
	return (time_t)atoll(Src);
}

char* getPiece( char* Dest, char* Src, int Max ) {
	int i=0;
	while( *Src && (*Src <=' ' || *Src == '=') )
		Src++;
	while( *Src && *Src != ' ' && *Src != '=' && i++ < Max)
		*Dest++ = *Src++;
	*Dest = '\0';
	while( *Src <= ' ' || *Src=='=' )
		Src++;
	return Src;
}

char* Trim( char*p ) {
	if( p ) {
		int l=strlen(p)-1;
		while( l>=0 && p[l]<=' ' )
			*(p+l--) = '\0';
	}
	return p;
}

void MyLog( char* Buff, int Len ) {
	char CRLF[]="\n";
	FILE *fp;
	time_t rawtime;
	char TimeStr[64];

	time( &rawtime );
	strftime( TimeStr, sizeof(TimeStr), "%Y-%m-%d %H:%M:%S", localtime( &rawtime ) );
	Trim( TimeStr );
	if( Len <= 0 )
		Len = strlen( Buff );

	fp = fopen( LogFile, "a+" );
	if( fp ) {
		
		fprintf( fp, "%s ", TimeStr );
		fwrite( Buff, 1, Len, fp );
		fprintf( fp, "\n");
		fclose( fp );
	}
	else {
		perror( "log" );
	}
}

void MyErrLog( char* Buf ) {
	sprintf( Emergency, "%s %s", Buf, strerror( errno ) );
	MyLog( Emergency, 0 );
}

void Myperror( char* Str ) {
	if( IsDaemon )
		perror( Str );
}

char* FileCat( char* Dest, char* Path, char*File ) {
	strcpy( Dest, Path );
	if( Dest[0] ) {
		if( Dest[strlen(Dest)-1] != '/' )
			strcat( Dest, "/" );
	}
	strcat( Dest, File );
	return Dest;
}

// Node functions
int timeDiffNode( NodeList* List, int Index ) {
	time_t tm;
	time( &tm );
	return (tm - List->Nodes[ Index ].Reference );
}
void touchNode( NodeList* List, int Index ) {
	time_t tm;
	time( &tm );
	List->Nodes[ Index ].Reference = (unsigned int)tm;
}

int findNode( NodeList* List, char* Str ) {
	int i=0;
	for( i=0; i< List->Count; i++ ) {
		if( strcmp(List->Nodes[ i ].Triplet, Str ) == 0 ) 
			return i;
	}
	return -1;
}

void deleteNode( NodeList* List, int Index ) {
	if( Index >=0 && Index < List->Count ) {
		List->Count--;
		free( List->Nodes[Index].Triplet );
		if( Index < List->Count )
			memmove( List->Nodes+Index, List->Nodes+Index+1, (List->Count - Index + 1)*sizeof(Node));
	}
}

void deleteNodeStr( NodeList* List, char* Str ) {
	int Index = findNode( List, Str );
	if( Index >=0 )
		deleteNode( List, Index );
}

void purgeNodes() {
	// purge old nodes from greylist and whitelist
	int i;
	time_t WhiteAge, GreyAge;

	time( &GreyAge );
	
	WhiteAge = GreyAge - Expire;
	GreyAge -= RetryMax;

	if( LastPurge > GreyAge )
		return;

	for( i=GreyList.Count-1; i >= 0; i-- )
		if( GreyList.Nodes[ i ].Reference < GreyAge )
			deleteNode( &GreyList, i );

	for( i=WhiteList.Count-1; i >= 0; i-- ) 
		if( WhiteList.Nodes[ i ].Reference < WhiteAge )
			deleteNode( &WhiteList, i );		
}

int addNode( NodeList* List, char* Str, time_t Reference ) {

	int Cur;
	char* Dup;
	time_t Now = Reference;

	if( Reference == 0 )
		time( &Reference );

	Cur = findNode( List, Str );
	if( Cur >=0 ) {
		List->Nodes[ Cur ].Reference = Reference;
		return 1;
	}

	Dup = strdup( Str );
	if( !Dup )
		return 0;


	if( List->Count==List->Max ) {
		// grow the list
		Node* Tmp = realloc( List->Nodes, (List->Max+GROWBY)*sizeof( Node ) );
		if( !Tmp ) {
			free( Dup );
			return 0;
		}
		List->Nodes = Tmp;
		List->Max += GROWBY;
	}
	List->Nodes[ List->Count ].Reference = Reference;
	List->Nodes[ List->Count++ ].Triplet = Dup;

	return 1;
}

int saveNodes( NodeList* List, char* Filename ) {
	FILE* fp;
	int i, n;

	if( (fp=fopen( Filename, "w+" )) ) {
		for( i=0; i < List->Count; i++ ) {
			n = fprintf( fp, "%u %s\n", (unsigned int)List->Nodes[i].Reference, List->Nodes[i].Triplet );
			if(  n< 0 ) {
				sprintf( Emergency, "Error saving list to %s", Filename );
				MyLog( Emergency, 0 ); // TODO: Report Errno data
				fclose( fp );
				return 0;
			}
		}
		fclose( fp );
	}
	else
		return 0;
}

void clearNodes( NodeList * List ) {
	int i;
	for( i=0; i < List->Count; i++ )
		free( List->Nodes[i].Triplet );
	free( List->Nodes );
	List->Nodes = NULL;
	List->Count = 0;
	List->Max=0;
}

int loadNodes( NodeList* List, char* Filename ) {
	FILE* fp;
	int i;
	unsigned int Reference;
	char* pRef;
	char* pStr;

	if( (fp=fopen( Filename, "r" )) ) {
		clearNodes( List );

		while( fgets( Emergency, sizeof(Emergency), fp ) ) {
			// TODO: User ferror
			Trim( Emergency );
			pRef = strtok( Emergency, " " );
			pStr = strtok( NULL, "!" );
			if( pRef && pStr )
				if( !addNode( List, pStr, strToTimeT(pRef) ) ) {
					MyErrLog( "loadNodes failed (out of memory?)" );
					fclose(fp);
					return 0;
				}
		}
		fclose( fp );
		return 1;
	}
	else
		return 1; // No load may mean file doesn't exist
	
}

int moveNode( NodeList* Dest, NodeList* Src, int Index ) {
	if( !addNode( Dest, Src->Nodes[ Index ].Triplet, Src->Nodes[ Index ].Reference ) )
		return 0;
	deleteNode( Src, Index );
	return 1;
}


void setup() {
	FILE* fp;
	char*s;
	char Cmd[100], Section[100];
	int n;
	
	// load config
	if( (fp=fopen( ConfigFile, "r" )) ) {
		while( fgets( Emergency, sizeof(Emergency), fp ) ) {
			s = Trim( Emergency );
			if( *s == '#' || !*s )
				continue;
			while( *s && *s <= ' ' )
				s++;
			s = getPiece( Cmd, s, sizeof(Cmd) );
			while( *s && (*s<=' ' || *s=='=' ) )
				s++;
			if( Cmd[0] == '[' ) {
				strcpy(Section, Cmd );
				continue;
			}
			if( strcasecmp( Section, "[timeouts]" ) == 0 ) {
				if( strcasecmp( Cmd, "retryMin" ) == 0 ) {
					RetryMin = atoi( s ); // We want to permit zero for testing
				}
				if( strcasecmp( Cmd, "retryMax" ) == 0 ) {
					n = atoi( s );
					if( n >= 0 )
						RetryMax = n;		
				}
			}
			if( strcasecmp( Section, "[socket]" ) == 0 ) {
				if( strcasecmp( Cmd, "path" ) == 0 ) {
					if( *s )
						strncpy( SocketFile, s, sizeof(SocketFile) );
				}
				if( strcasecmp( Cmd, "mode" ) == 0 ) {
					n = strtol( s, NULL, 0 );
					if( n )
						fileMode = n;
				}
			}
			if( strcasecmp( Section, "[data]" ) == 0 ) { 
				if( strcasecmp( Cmd, "update" ) == 0 ) {
					n = atoi( s );
					if( n )
						Update = n;
				}
				if( strcasecmp( Cmd, "pidpath" ) == 0 && *s ) {
					strncpy( LockFile, s, sizeof( LockFile ) );
				}
				if( strcasecmp( Cmd, "logfile" ) == 0 && *s ) {
					strncpy( LogFile, s, sizeof( LogFile ) );
				}
				if( strcasecmp( Cmd, "diag" ) == 0 )
					IsDiag = atoi( s );
			}
		}
	}
	time( &NextSave );
	NextSave += Update;
}

int isCmd( const char* Str ) {
	char*p=NULL;
	if( strncasecmp( Str, cmdADD, strlen(cmdADD) ) == 0 )		 p=cmdADD;
	if( strncasecmp( Str, cmdDELETE, strlen(cmdDELETE) ) == 0 )	 p=cmdDELETE;
	if( strncasecmp( Str, cmdCHECK, strlen( cmdCHECK) ) == 0 )	 p=cmdCHECK;
	if( strncasecmp( Str, cmdSTATS, strlen( cmdSTATS) ) == 0 )	 p=cmdSTATS;
	if( strncasecmp( Str, cmdMRTG, strlen( cmdMRTG) ) == 0 )	 p=cmdMRTG;
	if( strncasecmp( Str, cmdLIST, strlen( cmdLIST) ) == 0 )	 p=cmdLIST;
	if( strncasecmp( Str, cmdSAVE, strlen( cmdSAVE) ) == 0 )	 p=cmdSAVE;
	if( strncasecmp( Str, cmdRELOAD, strlen( cmdRELOAD) ) == 0 ) p=cmdRELOAD;
	if( strncasecmp( Str, cmdCLEAR, strlen( cmdCLEAR) ) == 0 )	 p=cmdCLEAR;
	if( strncasecmp( Str, cmdUPDATE, strlen( cmdUPDATE) ) == 0 ) p=cmdUPDATE;
	
	if( p ) {
		int i = strlen(p);
		if( Str[i] && Str[i]<=' ')
			return 1;
	}
	return 0;
}

int isList( const char* Str ) {
	if( strncasecmp( Str, listGREY, strlen(listGREY) ) == 0 )	return 1;
	if( strncasecmp( Str, listWHITE, strlen(listWHITE) ) == 0 )	return 1;
	if( strncasecmp( Str, listBLACK, strlen(listBLACK) ) == 0 )	return 1;
	return 0;
}

// Functions to process commands
char* doAdd( char* List, char* Str ) {
	if( strcmp( List, listWHITE ) == 0 )
		addNode( &WhiteList, Str, 0 );
	else
		deleteNodeStr( &WhiteList, Str );
	if( strcmp( List, listGREY ) == 0 )
		addNode( &GreyList, Str, 0 );
	else
		deleteNodeStr( &GreyList, Str );
	if( strcmp( List, listBLACK ) == 0 )
		addNode( &BlackList, Str, 0 );
	else
		deleteNodeStr( &BlackList, Str );

	return respTRUE;
}

char* doDelete( char* List, char* Text ) {
	if( !*List || strcmp( List, listWHITE ) == 0 )
		deleteNodeStr( &WhiteList, Text );
	if( !*List || strcmp( List, listGREY ) == 0 )
		deleteNodeStr( &GreyList, Text );
	if( !*List || strcmp( List, listBLACK ) == 0 )
		deleteNodeStr( &BlackList, Text );
	return respTRUE;
}

char* doCheck( char* List, char* Text ) {
	NodeList* pDest;
	char* Ret;

	if( !*List ) {
		if( findNode( &GreyList, Text ) >=0 )
			Ret = respGREY;
		else if( findNode( &BlackList, Text ) >=0 )
			Ret = respBLACK;
		else if( findNode( &WhiteList, Text ) >=0 )
			Ret = respWHITE;
		else 
			Ret = respNONE;
		return Ret;
	}
	if( strcmp( List, listWHITE ) == 0 )
		pDest = &WhiteList;
	if( strcmp( List, listGREY ) == 0)
		pDest = &GreyList;
	if( strcmp( List, listBLACK ) == 0)
		pDest = &BlackList;

	if( findNode( pDest, Text ) >=0 )
		Ret = respTRUE;
	else
		Ret = respFALSE;
	return Ret;
}

char* doUpdate( char* List, char* Text ) {
	int Index;
	if( strcmp( List, listGREY ) == 0 ) {

		if( findNode( &BlackList, Text ) >=0 ) {
			if( IsDiag )
				MyLog( "Found in blacklist", 0 );
			return respTRUE;
		}
		else if( IsDiag )
			MyLog( "Not found in blacklist", 0 );

		if( findNode( &WhiteList, Text ) >=0 ) {
			if( IsDiag )
				MyLog( "Found in whitelist", 0 );
			return respFALSE;
		}
		else if( IsDiag )
			MyLog( "Not found in whitelist", 0 );

		Index = findNode( &GreyList, Text );
		if( Index >= 0 ) {
			if( IsDiag )
				MyLog( "Found in greylist", 0 );
			// They retried, was it too soon?
			if( timeDiffNode( &GreyList, Index ) < RetryMin ) {
				touchNode( &GreyList, Index );
				if( IsDiag ) MyLog( "Too soon to move to whitelist", 0 );
				return respTRUE;
			}

			// If not, let them through
			if( !moveNode( &WhiteList, &GreyList, Index ) )
				MyLog( "Out of memory", 0 );
				
			return respFALSE;
		}
		else if( IsDiag )
			MyLog( "Not found in Greylist", 0 );
		if( !addNode( &GreyList, Text, 0 ) ) {
			MyLog( "Out of memory", 0 );
			return respFALSE; // if we failed, don't stop emails.
		}
		return respTRUE;
	}

	return respFALSE;
}

char* doStats() {
	if( IsDiag ) 
		MyLog( "Stats called, but not implemented", 0 );
	return respFALSE;
}
char* doMRTG() {
	if( IsDiag ) 
		MyLog( "MRTG called, but not implemented", 0 );
	return respFALSE;
}
char* doList( char* List, int Socket ) {
	char Str[256];
	NodeList* pList;
	int i;
	struct tm tms;
	time_t t;

	if( strcmp( List, listWHITE ) == 0 ) {
		strcpy( Str, "Whitelist data:\n===============\n" );
		pList = &WhiteList;
	}
	if( strcmp( List, listGREY ) == 0 ) {
		strcpy( Str, "Greylist data:\n==============\n" );
		pList = &GreyList;
	}
	if( strcmp( List, listBLACK ) == 0 ) {
		strcpy( Str, "Blacklist data:\n===============\n" );
		pList = &BlackList;
	}

	if (send( Socket, Str, strlen(Str), 0) < 0) {
		Myperror("send");
		return respERROR;
	}
	strcpy( Str, "  Last Seen            Count  Data\n" );
	if (send( Socket, Str, strlen(Str), 0) < 0) {
		Myperror("send");
		return respERROR;
	}

	for( i=0; i < pList->Count; i++ ) {
	//  2009-11-19 06:41:32      2  on
		strcpy( Str, "  " );

		strftime( Str+2, sizeof(Str)-3, "%Y-%m-%d %H:%M:%S", 
			localtime( &pList->Nodes[i].Reference) );
		strcat( Str, " " );
		sprintf( Str+strlen(Str), "%s\n", pList->Nodes[i].Triplet );
		if( send( Socket, Str, strlen(Str), 0 ) < 0 ) {
			Myperror("send");
			return respERROR;
		}
	}
	return "";
}

char* doSave( char* List ) {
	char Filename[256];
	
	if( !*List || strcmp( List, listWHITE )==0 ) {
		FileCat( Filename, DBPath, "white_list" );
		saveNodes( &WhiteList, Filename );
	}
	if( !*List || strcmp( List, listGREY )==0 ) {
		FileCat( Filename, DBPath, "grey_list" );
		saveNodes( &GreyList, Filename );
	}
	return respTRUE;
}

char* doReload( char* List ) {
	if( IsDiag ) 
		MyLog( "Reload called, but not implemented", 0 );
	return respFALSE;
}

char* doClear( char* List ) {
	if( IsDiag ) {
		sprintf( Emergency, "Clear %s", List );
		MyLog( Emergency, 0 );
	}

	if( !*List || strcmp( List, listWHITE ) == 0 )
		clearNodes( &WhiteList );
	if( !*List || strcmp( List, listGREY ) == 0 )
		clearNodes( &GreyList );
	if( !*List || strcmp( List, listBLACK ) == 0 )
		clearNodes( &BlackList );
	return respTRUE;
}

char* processCmd( char* Str, int Socket ) {
	char Buff[256];
	char Cmd[16];
	char List[16];
	char* Ret;

	if( !Str )
		return respFALSE; // unknown, don't block it

	Trim(Str);

	purgeNodes();

	MyLog( "Command received:", 0 );
	MyLog( Str, 0 );

	// Cmd may be [cmd] [--list] host_addr sender_address localemail
	if( isCmd( Str ) )
		Str = getPiece( Cmd, Str, sizeof(Cmd)-1 );
	else
		strcpy( Cmd, cmdUPDATE );

	if( isList( Str ) )
		Str = getPiece( List, Str, sizeof(List)-1 );
	else {
		// Default lists depend on Cmd
		if( strcmp( Cmd, cmdADD ) == 0 )
			strcpy( List, listWHITE );
		else if( strcmp( Cmd, cmdCHECK ) == 0 )
			strcpy( List, "" );	
		else
			strcpy(List, listGREY);
	}

	// We should now have 3 pieces of data in Str
	if( strcmp( Cmd, cmdADD ) == 0 )
		Ret = doAdd( List, Str );
	else if( strcmp( Cmd, cmdDELETE ) == 0 )
		Ret = doDelete( List, Str );
	else if( strcmp( Cmd, cmdCHECK ) == 0 )
		Ret = doCheck( List, Str );
	else if( strcmp( Cmd, cmdUPDATE ) == 0 )
		Ret = doUpdate( List, Str );
	else if( strcmp( Cmd, cmdSTATS ) == 0 )
		Ret = doStats();
	else if( strcmp( Cmd, cmdMRTG ) == 0 )
		Ret = doMRTG();
	else if( strcmp( Cmd, cmdLIST ) == 0 )
		Ret = doList( List, Socket );
	else if( strcmp( Cmd, cmdSAVE ) == 0 )
		Ret = doSave( List );
	else if( strcmp( Cmd, cmdRELOAD ) == 0 )
		Ret = doReload( List );
	else if( strcmp( Cmd, cmdCLEAR ) == 0 )
		Ret = doClear( List );
	else 
		Ret = respFALSE;
	
	MyLog( "Response: ", 0 );
	MyLog( Ret, 0 );

	return Ret;
}

int processSocket( int s2 ) {
	int done=0, n;
	char str[500];
	char* p;

	do {
		n = recv(s2, str, 499, 0);
		// assume for now that the entire command came in with 1 read
		if (n <= 0) {
			if (n < 0) 
				Myperror("recv");
			done = 1;
		}

		if (!done) {
			str[n]='\0';
			Trim( str );
			if( strcmp( str, "quit" ) == 0 ) {
				Terminate = 1;
				p = respBYE;
			}
			else
				p = processCmd( str, s2 );
			if (send(s2, p, strlen(p), 0) < 0) {
				Myperror("send");
				done = 1;
			}
			done =1;
		}
			
	} while (!done);

	close(s2);
}

int LoadData() {
	char Filename[256];
	time_t now;
	time( &now );
	
	FileCat( Filename, DBPath, "white_list" );
	if( ! loadNodes( &WhiteList, Filename ) )
		return 0;
	FileCat( Filename, DBPath, "grey_list" );
	if( !loadNodes( &GreyList, Filename ) )
		return 0;
	sprintf( Emergency, "Loaded lists: white=%d grey=%d", WhiteList.Count, GreyList.Count );
	MyLog( Emergency, 0 );
	return 1;
}

void SaveData( int Force ) {
	char Filename[256];
	time_t now;
	time( &now );
	
	if( now >= NextSave || Force) {
		FileCat( Filename, DBPath, "white_list" );
		saveNodes( &WhiteList, Filename );
		FileCat( Filename, DBPath, "grey_list" );
		saveNodes( &GreyList, Filename );
		time( &NextSave );
		NextSave += Update;
	}
}

int createListener() {
	int s, len;
	struct sockaddr_un local;

	if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		MyErrLog("socket");
		return 0;
	}

	local.sun_family = AF_UNIX;
	strcpy(local.sun_path, SocketFile );
	unlink(local.sun_path);
	len = strlen(local.sun_path) + sizeof(local.sun_family);
	if (bind(s, (struct sockaddr *)&local, len) == -1) {
		MyErrLog("bind");
		return 0;
	}

	chmod( SocketFile, fileMode );

	if (listen(s, 5) == -1) {
		MyErrLog("listen");
		return 0;
	}

	return s;
}

int getClient( int Socket ) {
	int s2, len;
	struct sockaddr_un remote;

	fd_set fdSet;
	struct timeval TimeToWait;
	int ready, t = sizeof( remote );

	TimeToWait.tv_sec = 2;
	TimeToWait.tv_usec = 0;
	FD_ZERO( &fdSet );
	FD_SET( Socket, &fdSet );

	ready = select( Socket+1, &fdSet, NULL, NULL, &TimeToWait );
	if( ready < 0 ) // Ignore error, but we assume it's EINTR
		return 0;

	if( ready ) {
		if ((s2 = accept(Socket, (struct sockaddr *)&remote, &t)) == -1) {
				MyErrLog("accept");
				return -1;
		}
		return s2;
	}

	return( 0 );
}

void terminate_handler(int sig)
{
	Terminate = 1;
}

void daemonize()
{
	int i,lfp;
	char str[10];


	if(getppid()==1) 
		return; // already a daemon
	
	i=fork();
	if (i<0) {
		MyErrLog( "Daemonize fork" );
		exit(1); // Error
	}
	if (i>0) { MyLog( "Parent stopped", 0 ); exit(0); } // We're the parent, exit

	MyLog( "daemon started", 0 );
	// At this point, we must be the child

  	setsid(); // obtain a new process group
	
	for (i=getdtablesize();i>=0;--i) 
		close(i); // Close standard file handles

	// re-create standard file handlers (stdin, stdout, etc)
	i=open("/dev/null",O_RDWR); 
	dup(i); 
	dup(i);

	umask(027); 

	// If already running, exit
	lfp=open(LockFile,O_RDWR|O_CREAT,0640);
	if (lfp<0) {
		MyErrLog( "Can't create lock file" );
		exit(1); /* can not open */
	}
	if (lockf(lfp,F_TLOCK,0)<0) {
		MyErrLog( "Can't lock file" );
		exit(0); // It's locked, so program is already running (not an error, just wierd)
	}

	// Save pid to lock file, so service (start/stop/restart) program can use it
	sprintf(str,"%d\n",getpid());
	write(lfp,str,strlen(str)); 

	// Ignore child, tty, output and input signals and hangup
	signal( SIGCHLD,SIG_IGN );
	signal( SIGTSTP,SIG_IGN );
	signal( SIGTTOU,SIG_IGN ); 
	signal( SIGTTIN,SIG_IGN );
	signal( SIGHUP,SIG_IGN );

	// Handle terminate signal
	signal( SIGTERM, terminate_handler );

	IsDaemon = 1;

	MyLog( "Daemonize ok", 0 );
}

int hasArg( int argc, char**argv, char*Str ) {
	int i;
	for( i=1; i<argc; i++ )
		if( strcmp( argv[i], Str ) == 0 )
			return 1;
	return 0;
}

int doExec( char* Cmd, char* Resp, int RespSize, char* Expected ) {
	int i;
    struct sockaddr_un remote;
    int s, t, len = sizeof( remote );

    remote.sun_family = AF_UNIX;
    strcpy(remote.sun_path, SocketFile);
	
    len = strlen(remote.sun_path) + sizeof(remote.sun_family);

    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }
    if (connect(s, (struct sockaddr *)&remote, len) == -1) {
        perror("connect");
        exit(1);
    }

	if (send(s, Cmd, strlen(Cmd), 0) == -1) {
		perror("send");
		exit(1);
	}

	while ((t=recv(s, Resp, RespSize, 0)) > 0) {
		Resp[t] = '\0';
		close(s);
	}
	if( Expected && strcasecmp( Resp, Expected ) )
	{
		if( *Expected )
			printf( "\nExpected '%s' but got '%s':\n%s\n", Expected, Resp, Cmd );
		return 0;
	}

	return 1;
}

void doKill() {
	// A neat way to kill the mgreylistd service
	// Note: If any user has access to the socket file, then
	// and user can terminate it.
	char Resp[100];
	doExec( "quit", Resp, sizeof(Resp), NULL );
}

int main( int argc, char** argv )
{
	int sListener, sClient;
	int i;
	signal( SIGTERM, terminate_handler );
	if( hasArg( argc, argv, "--version" ) ) {
		printf( "mgreylistd %s\n", VERSION );
		printf( "Copyright (C) 2009 Mario Giannini\n" );
		printf( "License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>.\n" );
		printf( "This is free software: you are free to change and redistribute it.\n" );
		printf( "There is NO WARRANTY, to the extent permitted by law.\n\n" );
		printf( "Written by Mario Giannini.\n" );
		exit(0);
	}

	if( hasArg( argc, argv, "-k" ) ) {
		doKill();
		exit(0);
	}
	if( hasArg( argc, argv, "-d" ) )
		daemonize();

	setup();

	if( IsDiag ) 
		MyLog( "Diagnostics set", 0 );

	if( !LoadData() ) {
		MyLog( "Data load failed, terminating", 0 );
		exit(0);
	}
	sListener = createListener();
	if( sListener==0 )
	{
		MyLog( "Data load failed, terminating", 0 );
		exit( 1 );
	}
	sClient = 0;
	while( !Terminate && sClient >=0 ) {
		// Time to save?
		SaveData( 0 );
		sClient = getClient( sListener );
		if( sClient > 0 )
		{
			if( IsDiag ) 
				MyLog( "ProcessSocket:", 0 );
			processSocket( sClient );
		}

	}
	SaveData( 1 );
	close( sListener );
	unlink( SocketFile );
	unlink( LockFile );
	if( IsDaemon )
		MyLog( "daemon stopped",0 );
	exit(0);
}

