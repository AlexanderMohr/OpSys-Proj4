/* Operating systems project 4
   Code written by Alexander Mohr
*/

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#include <pthread.h>

#define BUFFER_SIZE 1024
#define n_blocks 128
#define blocksize 4096.0
#define numClients 5//5 is the max number of waiting clients- can be changed here if necessary

int freeBlocks = n_blocks;
char* nameFree[26];//tracks file names in memory management simulation, 'A' - 'Z'
                    //if the letter is taken, nameFree[i] will hold the filename corresponding to the letter
                    //otherwise nameFree[i][0] == '\0'

char memory[n_blocks];
unsigned int tid;
pthread_t tids[numClients];//each client will be a thread
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;//used whenever file manipulation/reading is occuring

void * childThread(void * arg);

typedef struct argument_info
{
  struct sockaddr_in client;
  int sock;
} arg_t;
arg_t arguments[numClients];

//Return 1 if file exists, 0 if not
int fileExists(const char *filename) 
{
    struct stat st;
    int result = stat(filename, &st);
    
    return result == 0;
}

int StartsWith(const char *a, const char *b)
{
   if(strncmp(a, b, strlen(b)) == 0) return 1;
   return 0;
}

int main()
{  
  tid = (unsigned int)pthread_self();
  char folderName[100];
  strcpy(folderName, "storage");
  
  int i;
  for (i = 0; i < n_blocks; i++)
  {
    memory[i] = '.';    
  }
  for (i = 0; i < 26; i++)
  {
    nameFree[i] = (char *)malloc(sizeof(char));
    nameFree[i][0] = '\0';
  }
  printf("Block size is %G\nNumber of blocks is %d\n", blocksize, n_blocks);
  
  //Make a folder to store files if the folder does not already exist and change
  // the current working directory to that folder
  mkdir(folderName, S_IRWXU | S_IRWXG);
  chdir(folderName);
  
  DIR *d;
  struct dirent *dir;
  d = opendir(".");
  if (d)//Read all the file names
  {
    while ((dir = readdir(d)) != NULL)
    {
      if (dir->d_type == DT_REG)
      {
        int del = remove(dir->d_name);
          
        if (del != 0)//file wasn't deleted successfully
        {
          perror("remove() failed");
        }
      }
    }
    closedir(d);
  }
  
  
  
  //Create the listener socket as TCP socket
  int sock = socket( PF_INET, SOCK_STREAM, 0 );

  if ( sock < 0 )
  {
    perror( "socket() failed" );
    exit( EXIT_FAILURE );
  }

  //socket structures
  struct sockaddr_in server;

  server.sin_family = PF_INET;
  server.sin_addr.s_addr = INADDR_ANY;

  unsigned short listener_port = 8765;

  server.sin_port = htons( listener_port );
  int len = sizeof( server );

  if ( bind( sock, (struct sockaddr *)&server, len ) < 0 )
  {
    perror( "bind() failed" );
    exit( EXIT_FAILURE );
  }

  listen( sock, numClients );   
  for (i = 0; i < numClients; i++)//initialize all to parent's tid to show that there are no child threads right now
  {
    tids[i] = tid; 
  }
  
  printf( "Listening on port %d\n", listener_port );

  struct sockaddr_in client;
  int fromlen = sizeof( client );

  while ( 1 )
  {
    int newsock = accept( sock, (struct sockaddr *)&client, (socklen_t*)&fromlen );

    int rc;
    long offset;
    //handle socket in a child thread
    for (offset = 0; offset < numClients; offset++)
    {
      if (tids[offset] == tid)
      {
        arguments[offset].client = client;
        arguments[offset].sock = newsock;
        rc = pthread_create(&tids[offset], NULL, childThread, (void *)offset);
        break;
      }
    }
    
    if (offset == numClients)
    {
      close(newsock); 
    }
    
    //pthread failed
    if ( rc != 0 )
    {
        fprintf( stderr, "pthread_create() failed (%d): %s", rc, strerror( rc ) );
    }
    
  }
  
  for ( i = 0 ; i < numClients ; i++ )
  {
    if (tids[i] != tid)
    {
      pthread_join( tids[i], NULL);
    }
  }
  close( sock );

  return EXIT_SUCCESS;
}

void * childThread(void * arg)
{
  long offset = (long)arg;
  
  struct sockaddr_in client = arguments[offset].client;
  int newsock = arguments[offset].sock;
  int n;
  char buffer[ BUFFER_SIZE ];
  printf("Received incoming connection from %s\n", inet_ntoa( (struct in_addr)client.sin_addr ));
  do
  {
    
    //Read input
    n = recv( newsock, buffer, BUFFER_SIZE, 0 );
    
    if ( n < 0 )
    {
      perror( "recv() failed" );
    }
    else if (n != 0)
    {
      buffer[n] = '\0';
      printf( "[thread %u] Rcvd: %s", (unsigned int)pthread_self(), buffer );
      
      char message[1024];
      
      
      //Checks the input for one of the accepted comnmands
      if (StartsWith(buffer, "STORE"))
      {
        char filename[strlen(buffer) - 6];
        unsigned int i = 0;
        int j = 0;
        for (i = 6; i < strlen(buffer); i++)//read filename
        {
          if (buffer[i] == ' ')
          {
             break;
          }
          else
          {
            filename[j] = buffer[i];
            j++;
          }
        }
        filename[j] = '\0';
        i++;
        
        if (i >= strlen(buffer))//Not enough information was provided
        {
          strcpy(message, "ERROR: Could not understand command. Expected 'STORE <filename> <bytes>'\n");
          
          n = send(newsock, message, strlen(message), 0);
          fflush(NULL);
          if (n != strlen(message))
          {
            perror("send() failed");
          }
          else
          {
            printf("[thread %u] Sent: %s", (unsigned int)pthread_self(), message); 
          }
        }
        else
        {
          j = 0;
          char temp[strlen(buffer) - i];
          for (i = i; i < strlen(buffer); i++)//read the number of bytes the client wants to send
          {
             if (buffer[i] == '\n')
             {
                break; 
             }
             else
             {
                temp[j] = buffer[i];
                j++;
             }
          }
          temp[j] = '\0';
          int bytes = atoi(temp);
          
          n = recv( newsock, buffer, BUFFER_SIZE, 0);//get the information the client sent
          buffer[n] = '\0';
          
          printf( "[thread %u] Rcvd: %s", (unsigned int)pthread_self(), buffer );
          
          pthread_mutex_lock( &mutex );
          if (fileExists(filename))//Alert the user if file already exists
          {
            strcpy(message, "ERROR: FILE EXISTS\n");
            n = send(newsock, message, strlen(message), 0);
            fflush(NULL);
            if (n != strlen(message))
            {
              perror("send() failed");
            }
            else
            {
              printf("[thread %u] Sent: %s", (unsigned int)pthread_self(), message); 
            }
          }
          else
          {
            int reqBlocks = (bytes + blocksize - 1) / blocksize;//round up
            
            if (reqBlocks <= freeBlocks)
            {
              //memory management simulation
              for (i = 0; i < 26; i++)
              { 
                if (nameFree[i][0] == '\0')
                {
                   strcpy(nameFree[i], filename);
                   break;
                }
              }
              
              if (i == 26)//no characters left to assign to the file, pretend we don't have enough space on disk
              {
                strcpy(message, "ERROR: INSUFFICIENT DISK SPACE\n");
                n = send(newsock, message, strlen(message), 0);
                fflush(NULL);
                if (n != strlen(message))
                {
                  perror("send() failed");
                } 
                else
                {
                  printf("[thread %u] Sent: %s", (unsigned int)pthread_self(), message); 
                }                
              }
              else//store the file
              {
                freeBlocks -= reqBlocks;
                char fileLetter = 'A';
                fileLetter += i;
                int rem = reqBlocks;
                int clusters = 0;
                int foundBlock = 0;
                for (i = 0; i < n_blocks; i++)
                {
                  if (foundBlock == 0)
                  {
                    foundBlock = 1;
                    clusters++;
                  }
                  
                  if (memory[i] == '.')
                  {
                    memory[i] = fileLetter;
                    rem--;
                    if (rem == 0)
                    {
                      break; 
                    }
                  }
                  else if (foundBlock == 1)
                  {
                    foundBlock = 0;
                  }
                }
                printf("[thread %u] Stored file '%c' (%d bytes; %d blocks; %d cluster(s)\n", (unsigned int)pthread_self(),
                        fileLetter, bytes, reqBlocks, clusters);
                printf("[thread %u] Simulated Clustered Disk Space Allocation:\n================================\n", 
                        (unsigned int)pthread_self());
                for (i = 0; i < n_blocks; i++)
                {
                  printf("%c", memory[i]);
                  if (i % 32 == 31)
                  {
                    printf("\n"); 
                  }
                }
                printf("================================\n");
                
                
                //Write to the file
                FILE *fp;
                fp = fopen(filename, "w");
                if (fp == NULL)
                {
                  perror("fopen() failed");
                  
                  strcpy(message, "ERROR: Could not create file\n");
                  n = send(newsock, message, strlen(message), 0);
                  fflush(NULL);
                  if (n != strlen(message))
                  {
                    perror("send() failed"); 
                  }
                  else
                  {
                    printf("[thread %u] Sent: %s", (unsigned int)pthread_self(), message); 
                  }
                }
                else
                {
                  
                  if (n > bytes + 1)
                  {
                    sprintf(message, "ERROR: Too much data. Ignoring last %d byte(s)\n", n - bytes - 1);
                    n = send(newsock, message, strlen(message), 0);
                    if (n != strlen(message))
                    {
                      perror("send() failed"); 
                    }
                    else
                    {
                      printf("[thread %u] Sent: %s", (unsigned int)pthread_self(), message); 
                    }
                  }
                  buffer[bytes] = '\0';
                  fprintf(fp, "%s", buffer);                  
                  fclose(fp);
                  
                  // send ack message back to the client
                  n = send( newsock, "ACK\n", 4, 0 );
                  fflush( NULL );
                  if ( n != 4 )
                  {
                    perror( "send() failed" );
                  }
                  else
                  {
                    printf("[thread %u] Sent: ACK\n", (unsigned int)pthread_self()); 
                  }
                }
              }
            }
            else
            {
              strcpy(message, "ERROR: INSUFFICIENT DISK SPACE\n");
              n = send(newsock, message, strlen(message), 0);
              fflush(NULL);
              if (n != strlen(message))
              {
                perror("send() failed");
              }
              else
              {
                printf("[thread %u] Sent: %s", (unsigned int)pthread_self(), message); 
              }
            }
            
            
          }
          pthread_mutex_unlock( &mutex );
        }
        
      }
      else if (StartsWith(buffer, "READ"))
      {
        char filename[strlen(buffer) - 5];
        unsigned int i = 0;
        int j = 0;
        for (i = 5; i < strlen(buffer); i++)//get the filename
        {
          if (buffer[i] == ' ')
          {
             break;
          }
          else
          {
            filename[j] = buffer[i];
            j++;
          }
        }
        filename[j] = '\0';
        i++;
        j = 0;
        
        char readOffset[strlen(buffer) - i];            
        for (i = i; i < strlen(buffer); i++)//get the offset
        {
          if (buffer[i] == ' ')
          {
            break; 
          }
          else
          {
             readOffset[j] = buffer[i];
             j++;
          }
        }
        readOffset[j] = '\0';
        i++;
        j = 0;
        char readLength[strlen(buffer) - i];
        for (i = i; i < strlen(buffer); i++)//get the length
        {
          if (buffer[i] == '\n')
          {
            break; 
          }
          else
          {
            readLength[j] = buffer[i];
            j++;
          }
        }
        readLength[j] = '\0';
        
        if (i > strlen(buffer))
        {
          strcpy(message, "ERROR: Could not understand command. Expected 'READ <filename> <byte offset> <length>'\n");
          n = send(newsock, message, strlen(message), 0);
          fflush(NULL);
          if (n != strlen(message))
          {
            perror("send() failed");
          }
          else
          {
            printf("[thread %u] Sent: %s", (unsigned int)pthread_self(), message); 
          }
        }
        else
        {
          int offset = atoi(readOffset);
          int length = atoi(readLength);
          
          pthread_mutex_lock( &mutex );
          if (fileExists(filename))
          {
            FILE *fp = fopen(filename, "r"); 
            
            fseek(fp, 0L, SEEK_END);
            int len = ftell(fp);//get file size to make sure we aren't trying to read more than exists
            
            if (len < offset + length || offset < 0 || len < 0)
            {
              strcpy(message, "ERROR: INVALID BYTE RANGE\n");
              n = send(newsock, message, strlen(message), 0);
              fflush(NULL);
              if (n != strlen(message))
              {
                perror("send() failed"); 
              }
              else
              {
                printf("[thread %u] Sent: %s", (unsigned int)pthread_self(), message); 
              }
            }
            else//read data and send it to user
            {
              fseek(fp, offset, SEEK_SET);//start at the offset
              char output[length + 1];
              fread(output, length, 1, fp);
              output[length] = '\0';
              sprintf(message, "ACK %d\n%s\n", length, output);
              
              n = send(newsock, message, strlen(message), 0);
              fflush(NULL);
              if (n != strlen(message))
              {
                perror("send() failed"); 
              }
              else
              {
                //calculate the blocks that were read from and round up
                int reqBlocks = (length + blocksize - 1 + (offset % (int)blocksize)) / blocksize;
                
                for (i = 0; i < 26; i++)
                {
                  if (strcmp(nameFree[i], filename) == 0)
                  {
                    break; 
                  }
                }
                char fileLetter = 'A';
                fileLetter += i;
                printf("[thread %u] Sent: %d bytes (from %d '%c' blocks) from offset %d\n", (unsigned int)pthread_self(),
                        length, reqBlocks, fileLetter, offset); 
              }
            }
          }
          else
          {
            strcpy(message, "ERROR: NO SUCH FILE\n");
            n = send(newsock, message, strlen(message), 0);
            fflush(NULL);
            if (n != strlen(message))
            {
              perror("send() failed"); 
            }
            else
            {
              printf("[thread %u] Sent: %s", (unsigned int)pthread_self(), message); 
            }
          }
          pthread_mutex_unlock( &mutex );
        }
      }
      else if (StartsWith(buffer, "DELETE"))
      {
        char filename[strlen(buffer) - 7];
        unsigned int i = 0;
        //Read the file name
        for (i = 7; i < strlen(buffer); i++)
        {
          if (buffer[i] == '\n')
          {
            break; 
          }
          else
          {
             filename[i - 7] = buffer[i];
          }
        }
        filename[i - 7] = '\0';
        
        if (buffer[i] != '\n')//file name could not be read
        {
          strcpy(message, "ERROR: Could not understand command. Expected 'DELETE <filename>'\n");
          n = send(newsock, message, strlen(message), 0);
          fflush(NULL);
          if (n != strlen(message))
          {
            perror("send() failed"); 
          }
          else
          {
            printf("[thread %u] Sent: %s", (unsigned int)pthread_self(), message); 
          }
        }
        else
        {
          pthread_mutex_lock( &mutex );
          //check if file exists and attempt to delete
          if (fileExists(filename))
          {
            for (i = 0; i < 26; i++)
            {
              if (strcmp(nameFree[i], filename) == 0)
              {
                nameFree[i][0] = '\0';
                break;
              }
            }
            
            char fileLetter = 'A';
            fileLetter += i;
            int addBlocks = 0;
            for (i = 0; i < n_blocks; i++)
            {
              if (memory[i] == fileLetter)
              {
                memory[i] = '.';
                addBlocks++;
              }
            }
            freeBlocks += addBlocks;
            
            printf("[thread %u] Deleted %s file '%c' (deallocated %d blocks)\n", (unsigned int)pthread_self(),
                    filename, fileLetter, addBlocks);
            printf("[thread %u] Simulated Clustered Disk Space Allocation:\n================================\n", 
                    (unsigned int)pthread_self());
            
            for (i = 0; i < n_blocks; i++)
            {
              printf("%c", memory[i]);
              if (i % 32 == 31)
              {
                printf("\n"); 
              }
            }           
            printf("================================\n");
            int del = remove(filename);
            
            if (del == 0)//file was deleted successfully
            {
              // send ack message back to the client
              n = send( newsock, "ACK\n", 4, 0 );
              fflush( NULL );
              if ( n != 4 )
              {
                perror( "send() failed" );
              }
              else
              {
                printf("[thread %u] Sent: ACK\n", (unsigned int)pthread_self()); 
              }
            }
            else//file was not deleted successfully
            {
              perror("remove() failed");
              
              strcpy(message, "Failed to delete file\n");
              n = send(newsock, message, strlen(message), 0);
              fflush(NULL);
              if (n != strlen(message))
              {
                perror("send() failed"); 
              }
              else
              {
                printf("[thread %u] Sent: %s", (unsigned int)pthread_self(), message); 
              }
            }
          }
          else//tell the client that the file does not exist
          {
            strcpy(message, "ERROR: NO SUCH FILE\n");
           
            n = send(newsock, message, strlen(message), 0);
            fflush(NULL);
            if (n != strlen(message))
            {
              perror("send() failed"); 
            }
            else
            {
              printf("[thread %u] Sent: %s", (unsigned int)pthread_self(), message); 
            }
          } 
          pthread_mutex_unlock( &mutex );
        }
      }
      else if (StartsWith(buffer, "DIR"))
      {
        char** fileList = malloc(0);
        int numFiles = 0;
        DIR *d;
        struct dirent *dir;
        pthread_mutex_lock( &mutex );
        d = opendir(".");
        if (d)//Read all the file names from the current directory (the storage folder)
        {
          while ((dir = readdir(d)) != NULL)
          {
              if (dir->d_type == DT_REG)
              {
                numFiles++;
                fileList = realloc(fileList, numFiles * sizeof(char*));
                fileList[numFiles - 1] = malloc(strlen(dir->d_name) * sizeof(char));
                strcpy(fileList[numFiles - 1], dir->d_name);
              }
          }
          closedir(d);
        }
        pthread_mutex_unlock( &mutex );
        
        //sort the filenames alphabetically
        int i, j;
        char temp[1024];
        for (i = 0; i < numFiles; i++)
        {
           for (j = 0; j < numFiles - 1; j++)
           {  
             if (strcmp(fileList[j], fileList[j + 1]) > 0)
             {
                strcpy(temp, fileList[j]);
                strcpy(fileList[j], fileList[j+1]);
                strcpy(fileList[j+1], temp);
             }
           }
        }
        
        sprintf(message, "%d\n", numFiles);
        
        //Append all file names onto a single string to be sent
        for (i = 0; i < numFiles; i++)
        {
          strcpy(message, strcat(message, fileList[i]));
          strcpy(message, strcat(message, "\n"));
        }
        
        n = send(newsock, message, strlen(message), 0);
        fflush(NULL);
        if (n != strlen(message))
        {
          perror("send() failed"); 
        }
        else
        {
          printf("[thread %u] Sent: %s", (unsigned int)pthread_self(), message); 
        }
      }
      else//unrecognized command
      {
        strcpy(message, "ERROR: That command does not exist. Commands are case-sensitive\n");
        n = send(newsock, message, strlen(message), 0);
        fflush(NULL);
        if (n != strlen(message))
        {
          perror("send() failed"); 
        }
        else
        {
          printf("[thread %u] Sent: %s", (unsigned int)pthread_self(), message); 
        }
      }
      
    }
  }
  while ( n > 0 );
  /* this do..while loop exits when the recv() call
  returns 0, indicating the remote/client side has
  closed its socket */
  
  printf( "[thread %u] Client closed its socket....terminating\n", (unsigned int)pthread_self());
  close( newsock );
  tids[offset] = tid;
  pthread_exit(NULL);  //thread terminates here
}