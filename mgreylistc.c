#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <error.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define SOCK_PATH "/var/run/mgreylistd/socket"

char* getPiece( char* Dest, char* Src, int Max ) {
	int i=0;
	while( *Src && *Src > ' ' && i++ < Max)
		*Dest++ = *Src++;
	*Dest = '\0';
	while( *Src <= ' ' )
		Src++;
	return Src;
}

int doExec( char* Cmd, char* Resp, int RespSize, char* Expected ) {
	int i;
    int s, t, len;
    struct sockaddr_un remote;

    remote.sun_family = AF_UNIX;
    strcpy(remote.sun_path, SOCK_PATH);
	
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
	if( strcasecmp( Resp, Expected ) )
	{
		printf( "\nExpected '%s' but got '%s':\n%s\n", Expected, Resp, Cmd );
		return 0;
	}
	return 1;
}

void doTest(int Max, int Loops) {
	char Cmd[256];
    char str[256];
	int i, Loop, Cont=1;

	doExec( "clear --grey", str, sizeof(str), "true" );
	doExec( "clear --white", str, sizeof(str), "true" );
	doExec( "clear --black", str, sizeof(str), "true" );
	
	for( Loop=1; Loop < Loops+1; Loop++ ) {
		printf( "%d: ", Loop ); fflush( stdout );

		printf("UPD A ", Loop); fflush( stdout );
		for( i=0; Cont && i < Max; i++ ) {
			sprintf( 
				Cmd, 
				"update --grey 255.255.255.255 test%d_%d@somewhere.com recip%d_%d@dest.com",
				 i, Loop, i, Loop );
			Cont=doExec( Cmd, str, sizeof(str), "true" );
		}
		printf( "GCHK ", Loop ); fflush( stdout );
		for( i=0; Cont && i < Max; i++ ) {
			sprintf( Cmd, 
				"check 255.255.255.255 test%d_%d@somewhere.com recip%d_%d@dest.com", 
				i, Loop, i, Loop );
			Cont = doExec( Cmd, str, sizeof(str), "grey");
		}

		printf("UPD B ", Loop); fflush( stdout );
		for( i=0; Cont && i < Max; i++ ) {
			sprintf( Cmd, 
				"update --grey 255.255.255.255 test%d_%d@somewhere.com recip%d_%d@dest.com",
				i, Loop, i, Loop );
			Cont = doExec( Cmd, str, sizeof(str), "true");
		}
		printf( "WCHK ", Loop ); fflush( stdout );
		for( i=0; Cont && i < Max; i++ ) {
			sprintf( Cmd, 
				"check 255.255.255.255 test%d_%d@somewhere.com recip%d_%d@dest.com", 
				i, Loop, i, Loop );
			Cont = doExec( Cmd, str, sizeof(str), "grey"); // white
		}

		printf( "WADD ", Loop ); fflush( stdout );
		for( i=0; Cont && i < Max; i++ ) {
			sprintf( Cmd, 
				"add --white 255.255.255.255 atest%d_%d@somewhere.com arecip%d_%d@dest.com", 
				i, Loop, i, Loop );
			Cont = doExec( Cmd, str, sizeof(str), "true");
		}
		printf( "WCHK A ", Loop ); fflush( stdout );
		for( i=0; Cont && i < Max; i++ ) {
			sprintf( Cmd, 
				"check --white 255.255.255.255 atest%d_%d@somewhere.com arecip%d_%d@dest.com", 
				i, Loop, i, Loop );
			Cont = doExec( Cmd, str, sizeof(str), "true");
		}
		printf( "WCHK B ", Loop ); fflush( stdout );
		for( i=0; Cont && i < Max; i++ ) {
			sprintf( Cmd, 
				"check 255.255.255.255 atest%d_%d@somewhere.com arecip%d_%d@dest.com", 
				i, Loop, i, Loop );
			Cont = doExec( Cmd, str, sizeof(str), "white");
		}

		printf( "GADD ", Loop ); fflush( stdout );
		for( i=0; Cont && i < Max; i++ ) {
			sprintf( Cmd, 
				"add --grey 255.255.255.255 gtest%d_%d@somewhere.com arecip%d_%d@dest.com", 
				i, Loop, i, Loop );
			Cont = doExec( Cmd, str, sizeof(str), "true");
		}
		printf( "GCHK A ", Loop ); fflush( stdout );
		for( i=0; Cont && i < Max; i++ ) {
			sprintf( Cmd, 
				"check 255.255.255.255 gtest%d_%d@somewhere.com arecip%d_%d@dest.com", 
				i, Loop, i, Loop );
			Cont = doExec( Cmd, str, sizeof(str), "grey");
		}
		printf( "GCHK B  ", Loop ); fflush( stdout );
		for( i=0; Cont && i < Max; i++ ) {
			sprintf( Cmd, 
				"check --grey 255.255.255.255 gtest%d_%d@somewhere.com arecip%d_%d@dest.com", 
				i, Loop, i, Loop );
			Cont = doExec( Cmd, str, sizeof(str), "true");
		}

		printf( "!WCHK ", Loop ); fflush( stdout );
		for( i=0; Cont && i < Max; i++ ) {
			sprintf( Cmd, 
				"check --white 255.255.255.255 gtest%d_%d@somewhere.com arecip%d_%d@dest.com", 
				i, Loop, i, Loop );
			Cont = doExec( Cmd, str, sizeof(str), "false");
		}
		printf( "GUPD ", Loop ); fflush( stdout );
		for( i=0; Cont && i < Max; i++ ) {
			sprintf( Cmd, 
				"update --grey 255.255.255.255 gtest%d_%d@somewhere.com arecip%d_%d@dest.com", 
				i, Loop, i, Loop );
			Cont = doExec( Cmd, str, sizeof(str), "true");
		}
		printf( "\n" );
	}
}

int main(int argc, char** argv )
{
    int s, t, len;
    struct sockaddr_un remote;
    char str[256];
	char* p;
	char Cmd[100];
	char List[100];
	char Triplet[100];
	int Max=10, Loops=5;

	for( s=0; s < argc; s++ ) {
		if( argv[s][0]=='-') {
			if( argv[s][1]=='m' )
				Max = atoi( argv[s] + 2 );
			if( argv[s][1]=='l' )
				Loops = atoi( argv[s] + 2 );
		}
	}
    printf("Enter mgreylistd command, or test (Max=%d, Loops=%d)...\n", Max, Loops );

    remote.sun_family = AF_UNIX;
    strcpy(remote.sun_path, SOCK_PATH);
	
    len = strlen(remote.sun_path) + sizeof(remote.sun_family);

    while(printf("> "), fgets(str, 256, stdin), !feof(stdin)) {
		if( strncmp( str, "test", 4 ) == 0 ) {
			doTest( Max, Loops);
			continue;
		}
		p = getPiece( Cmd, str, 100 );
	    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
	        perror("socket");
	        exit(1);
	    }
	    if (connect(s, (struct sockaddr *)&remote, len) == -1) {
	        perror("connect");
	        exit(1);
	    }

        if (send(s, str, strlen(str), 0) == -1) {
            perror("send");
            exit(1);
        }

        while ((t=recv(s, str, 100, 0)) > 0) {
            str[t] = '\0';
            printf("%s\n", str);
			close(s);
			if( strcmp( str, "bye" ) == 0 )
				break;

        } 

    }
    return 0;
}
