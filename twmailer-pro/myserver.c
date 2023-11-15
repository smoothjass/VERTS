#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <dirent.h>
#include <ldap.h>
#include <lber.h>

///////////////////////////////////////////////////////////////////////////////
   ///////////////////////////////////////////////////////////////////////////////
   //                                                                           //
   // TWMailer Pro server winter term 2022                                      //    
   //                                                                           //
   // authors:                                                                  //
   // Jasmin Duvivié if21b145@technikum-wien.at                                 //
   // Si Si Sun if21b102@technikum-wien.at                                      //
   // whoever made the clientServerSample in moodle (Daniel Kienboeck?)         //
   //                                                                           //
   ///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
   ///////////////////////////////////////////////////////////////////////////////
   // enum to distinguish the type of request in a switch
   // none is only used to first initialize the type before we have parsed the request
enum command{
   none,
   sendMessage,
   listMessages,
   readMessage,
   deleteMessage,
   quit
};

///////////////////////////////////////////////////////////////////////////////

   ///////////////////////////////////////////////////////////////////////////////
   // inOrOut must be "/in/" or "/out/"
   // depending if the message should be persisted in the inbox of receiver or the outbox of sender
   // saveMail returns 1 on success and 0 on failure
int saveMail(char* user, char* sender, char* subject, char* message, char* inOrOut);

void listMail(char* username);
void readMail(char* username, int msgnumber);
void deleteMail(char* username, int msgnumber);

   ///////////////////////////////////////////////////////////////////////////////
   // errorhandling is a switch(errno),
   // those errnos that might be set by the called functions according to the man pages are handled
   // the function sets the response to "ERR" and the respective error message
void errorHandling(int error);

int ldapCredentials(char* fulluid, char* searchedUid, char* pwd);
///////////////////////////////////////////////////////////////////////////////

#define BUF 1024
#define PORT 6543

///////////////////////////////////////////////////////////////////////////////

int abortRequested = 0;
int create_socket = -1;
int new_socket = -1;

   ///////////////////////////////////////////////////////////////////////////////
   // globally scoped char array to set the response to the client
   // either OK or ERR and additional information
   // depending on the type of request that is being
   // responded to
char response[BUF];

///////////////////////////////////////////////////////////////////////////////

void *clientCommunication(void *data);
void signalHandler(int sig);

///////////////////////////////////////////////////////////////////////////////

int main(void)
{
   socklen_t addrlen;
   struct sockaddr_in address, cliaddress;
   int reuseValue = 1;

   ////////////////////////////////////////////////////////////////////////////
   // SIGNAL HANDLER
   // SIGINT (Interrup: ctrl+c)
   // https://man7.org/linux/man-pages/man2/signal.2.html
   if (signal(SIGINT, signalHandler) == SIG_ERR)
   {
      perror("signal can not be registered");
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // CREATE A SOCKET
   // https://man7.org/linux/man-pages/man2/socket.2.html
   // https://man7.org/linux/man-pages/man7/ip.7.html
   // https://man7.org/linux/man-pages/man7/tcp.7.html
   // IPv4, TCP (connection oriented), IP (same as client)
   if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
   {
      perror("Socket error"); // errno set by socket()
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // SET SOCKET OPTIONS
   // https://man7.org/linux/man-pages/man2/setsockopt.2.html
   // https://man7.org/linux/man-pages/man7/socket.7.html
   // socket, level, optname, optvalue, optlen
   if (setsockopt(create_socket,
                  SOL_SOCKET,
                  SO_REUSEADDR,
                  &reuseValue,
                  sizeof(reuseValue)) == -1)
   {
      perror("set socket options - reuseAddr");
      return EXIT_FAILURE;
   }

   if (setsockopt(create_socket,
                  SOL_SOCKET,
                  SO_REUSEPORT,
                  &reuseValue,
                  sizeof(reuseValue)) == -1)
   {
      perror("set socket options - reusePort");
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // INIT ADDRESS
   // Attention: network byte order => big endian
   memset(&address, 0, sizeof(address));
   address.sin_family = AF_INET;
   address.sin_addr.s_addr = INADDR_ANY;
   address.sin_port = htons(PORT);

   ////////////////////////////////////////////////////////////////////////////
   // ASSIGN AN ADDRESS WITH PORT TO SOCKET
   if (bind(create_socket, (struct sockaddr *)&address, sizeof(address)) == -1)
   {
      perror("bind error");
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // ALLOW CONNECTION ESTABLISHING
   // Socket, Backlog (= count of waiting connections allowed)
   if (listen(create_socket, 5) == -1)
   {
      perror("listen error");
      return EXIT_FAILURE;
   }

   while (!abortRequested)
   {
      /////////////////////////////////////////////////////////////////////////
      // ignore errors here... because only information message
      // https://linux.die.net/man/3/printf
      printf("Waiting for connections...\n");

      /////////////////////////////////////////////////////////////////////////
      // ACCEPTS CONNECTION SETUP
      // blocking, might have an accept-error on ctrl+c
      addrlen = sizeof(struct sockaddr_in);
      if ((new_socket = accept(create_socket,
                               (struct sockaddr *)&cliaddress,
                               &addrlen)) == -1)
      {
         if (abortRequested)
         {
            perror("accept error after aborted");
         }
         else
         {
            perror("accept error");
         }
         break;
      }
      /////////////////////////////////////////////////////////////////////////
      // FORKEN
      // parent closes new_socket (connectionsocket)
      // child closes create_socket (listeningsocket)
      // parent must wait at some point
      // NEED : Lock File Description For The Synchronization
      pid_t pid = fork();
      switch (pid)
      {
         case 0:
            // child. do stuff
            close(create_socket);
            /////////////////////////////////////////////////////////////////////////
            // START CLIENT
            // ignore printf error handling
            printf("Client connected from %s:%d...\n",
                  inet_ntoa(cliaddress.sin_addr),
                  ntohs(cliaddress.sin_port));
            clientCommunication(&new_socket); // returnValue can be ignored
            new_socket = -1;
            close(new_socket);
            break;
         default:
            // parent. do stuff
            printf("child pid: %d\n", pid);
            break;
      }
      /*
      /////////////////////////////////////////////////////////////////////////
      // START CLIENT
      // ignore printf error handling
      printf("Client connected from %s:%d...\n",
             inet_ntoa(cliaddress.sin_addr),
             ntohs(cliaddress.sin_port));
      clientCommunication(&new_socket); // returnValue can be ignored
      new_socket = -1;
      */
   }

   ///////////////////////////////////////////////////////////////////////////////
   // frees the descriptor
   if (create_socket != -1)
   {
      if (shutdown(create_socket, SHUT_RDWR) == -1)
      {
         perror("shutdown create_socket");
      }
      if (close(create_socket) == -1)
      {
         perror("close create_socket");
      }
      create_socket = -1;
   }
   
   // wait for all child
   while(wait(NULL) > 0);
   
   return EXIT_SUCCESS;
}

void *clientCommunication (void *data)
{
   char buffer[BUF];
   int size;
   int *current_socket = (int *)data;

   ////////////////////////////////////////////////////////////////////////////
   // SEND welcome message
   strcpy(buffer, "Welcome to myserver!\r\nPlease enter your commands...\r\nSEND\n<receiver>\n<subject>\n<message>\n.\nLIST\n.\nREAD\n<message number>\n.\nDEL\n<message number>\n.\n");
   if (send(*current_socket, buffer, strlen(buffer), 0) == -1)
   {
      perror("send failed");
      return NULL;
   }
   ////////////////////////////////////////////////////////////////////////////
   // LOGIN
   char rawuid[128];
   char fulluid[256];
   char pwd[256];
   int loginSuccess = 0;
   while(!loginSuccess)
   {
      for (int i = 0; i < 2; ++i)
      {
         size = recv(*current_socket, buffer, BUF - 1, 0);
         printf("bytes received: %d\n", size);
         if (size == -1)
         {
            if (abortRequested)
            {
               perror("recv error after aborted");
            }
            else
            {
               perror("recv error");
            }
            break;
         }
         ////////////////////////////////////////////////////////////////////////////
         // NEED : Wrong credentials does not work, program stops somewhere, not sure where
         if (size == 0)
         {
            printf("Client closed remote socket\n"); // ignore error
            break;
         }
         buffer[size] = '\0';
         if(strcmp(buffer, "quit\n.") == 0)
         {
            abortRequested = 1;
            break;
         }
         if (i == 0)
         {
            strcpy(rawuid,buffer);
            sprintf(fulluid, "uid=%s,ou=people,dc=technikum-wien,dc=at", rawuid);
         }
         else if (i == 1)
         {
            strcpy(pwd, buffer);
         }
      }
   
      loginSuccess = ldapCredentials(fulluid, rawuid, pwd);
      /////////////////////////////////////////////////////////////////////////
      // this is not printed when wrong uid or pw is entered, so we assume
      // that we get stuck in the ldapCredentials function somehow
      // but after trying to figure out the problem for some hours
      // we give up
      // login is now only possible, if correct credentials are entered
      // if wrong credentials are entered, programm stops. could be worse.
      printf("loginSuccess: %d\n", loginSuccess); 
      if(!loginSuccess)
      {
          //send not ok
         int bytesSent = send(*current_socket, "NOTOK", BUF -1, 0);
         printf("bytes sent: %d\n", bytesSent);
         if (bytesSent == -1){
            perror("send answer failed");
            return NULL;
         }
      }
      else
      {
         //send ok
         int bytesSent = send(*current_socket, "LOGINOK", BUF -1, 0);
         printf("bytes sent: %d\n", bytesSent);
         if (bytesSent == -1){
            perror("send answer failed");
            return NULL;
         }
         break;
      }
   }
   do
   {
      /////////////////////////////////////////////////////////////////////////
      // RECEIVE
      // we are not entirely sure why we had to receive BUF - 1 instead of BUF
      // as the original sample worked with just BUF
      // but we had segmentation faults until we changed it to BUF - 1 so
      // here we are
      size = recv(*current_socket, buffer, BUF - 1, 0);
      printf("bytes received: %d\n", size);
      if (size == -1)
      {
         if (abortRequested)
         {
            perror("recv error after aborted");
         }
         else
         {
            perror("recv error");
         }
         break;
      }

      if (size == 0)
      {
         printf("Client closed remote socket\n"); // ignore error
         break;
      }

      ///////////////////////////////////////////////////////////////////////////////
      // remove ugly debug message, because of the sent newline of client
      printf("buffer: %s\n", buffer);
      if (buffer[size - 2] == '\r' && buffer[size - 1] == '\n')
      {
         size -= 2;
      }
      else if (buffer[size - 1] == '\n')
      {
         --size;
      }

      buffer[size] = '\0';

      printf("Message received: %s\n", buffer); // ignore error

      ///////////////////////////////////////////////////////////////////////////////
      // parse request by splitting it into tokens seperated by a new line
      // first token sets the type
      // following tokens depend on the type
      // if request starts with a correct method but then gives not enough arguments:
      // program crashes -> :'(
      char delimeter[2] = "\n";
      char* token = strtok(buffer, delimeter);
    
      enum command type;
      type = none;

      if(strcmp(token, "SEND") == 0){
         type = sendMessage;
      }
      else if(strcmp(token, "LIST") == 0){
         type = listMessages;
      }
      else if(strcmp(token, "READ") == 0){
         type = readMessage;
      }
      else if(strcmp(token, "DEL") == 0){
         type = deleteMessage;
      }
      else if(strcmp(token, "quit") == 0){
         type = quit;
      }

      token = strtok(NULL, delimeter);

      ///////////////////////////////////////////////////////////////////////////////
      // variables to save tokens must be declared before switch to not go out of scope
      // but space is not an issue in this application so we don't mind

      //char username[strlen(token)]; //now in rawuid
      
      //char sender[strlen(token)]; //now in rawuid
      char receiver[BUF];
      char subject[BUF];
      char message[BUF];

      int msgnumber = 0;

      switch (type)
      {
         case sendMessage:
            ///////////////////////////////////////////////////////////////////////////////
            // parse: sender, receiver, subject, message
            //strcpy(sender, token); //now in rawuid
            //token = strtok(NULL, delimeter);
            strcpy(receiver, token);
            token = strtok(NULL, delimeter);
            strcpy(subject, token);
            token = strtok(NULL, delimeter);
            strcpy(message, token);
            if(ldapCredentials(fulluid, receiver, pwd)) // i.e. receiver has a valid account on ldap server so we can try and send a message
            { 
               int saveSuccess = saveMail(receiver, rawuid, subject, message, "/in/"); //save message to receivers inbox
               saveSuccess += saveMail(rawuid, rawuid, subject, message, "/out/"); //save message to senders outbox
               if(saveSuccess == 2) // both save operations successfull
               { 
                  strcpy(response, "OK\n");
               }
               else
               {
                  strcpy(response, "ERR\n");
               }
            }
            else // i.e. ldap query failed because receiver does not exist
            { 
               strcpy(response, "ERR - receiver does not exist\n");
            }
            break;
         case listMessages:
            ///////////////////////////////////////////////////////////////////////////////
            // parse: username
            //strcpy(username, token);
            listMail(rawuid);
            break;
         case readMessage:
            ///////////////////////////////////////////////////////////////////////////////
            // read and del: parse: username, message number
            //strcpy(username, token);
            //token = strtok(NULL, delimeter);
            msgnumber = atoi(token);
            readMail(rawuid, msgnumber);
            break;
         case deleteMessage:
            //strcpy(username, token);
            //token = strtok(NULL, delimeter);
            msgnumber = atoi(token);
            deleteMail(rawuid, msgnumber);
            break;
         case quit:
            strcpy(response, "OK - goodbye\n");
            break;
         default: strcpy(response, "ERR - wrong command");
            break;
      }
    
      ///////////////////////////////////////////////////////////////////////////////
      // response was set depending on request, operations performed and
      // if those were succesful or not
      // now send to client
      int bytesSent = send(*current_socket, response, BUF -1, 0);
      printf("bytes sent: %d\n", bytesSent);
      if (bytesSent == -1)
      {
         perror("send answer failed");
         return NULL;
      }
      response[0] = '\0';
   } while (strcmp(buffer, "quit\n.") != 0 && !abortRequested);

   ///////////////////////////////////////////////////////////////////////////////
   // closes/frees the descriptor if not already
   if (*current_socket != -1)
   {
      if (shutdown(*current_socket, SHUT_RDWR) == -1)
      {
         perror("shutdown new_socket");
      }
      if (close(*current_socket) == -1)
      {
         perror("close new_socket");
      }
      *current_socket = -1;
   }

   return NULL;
}

void signalHandler(int sig)
{
   if (sig == SIGINT)
   {
      printf("abort Requested... "); // ignore error
      abortRequested = 1;
      /////////////////////////////////////////////////////////////////////////
      // With shutdown() one can initiate normal TCP close sequence ignoring
      // the reference count.
      // https://beej.us/guide/bgnet/html/#close-and-shutdownget-outta-my-face
      // https://linux.die.net/man/3/shutdown
      if (new_socket != -1)
      {
         if (shutdown(new_socket, SHUT_RDWR) == -1){
            perror("shutdown new_socket");
         }
         if (close(new_socket) == -1){
            perror("close new_socket");
         }
         new_socket = -1;
      }

      if (create_socket != -1)
      {
         if (shutdown(create_socket, SHUT_RDWR) == -1){
            perror("shutdown create_socket");
         }
         if (close(create_socket) == -1){
            perror("close create_socket");
         }
         create_socket = -1;
      }
   }
   else
   {
      exit(sig);
   }
}

int saveMail(char* user, char* sender, char* subject, char* message, char* inOrOut)
{ 
   ///////////////////////////////////////////////////////////////////////////////
   // inOrOut must be "/in/" or "/out/"
   // depending if the message should be persisted in the inbox of receiver or the outbox of sender

   ///////////////////////////////////////////////////////////////////////////////
   // path to user directory is arranged with /var/spool/mail/<username>
   // if it does not exist already (mkdir(directory, 777) == 0), in and outbox must be created
   // if user does exist (errno == EEXIST), message is persisted in repsective subdirectory in or out
   // if a message with the same subject exists already in respective directory
   // then it is overwritten :(
   // if user directory could not be created to any other reasons (i.e. mkdir(directory, 777) != 0 but errno != EEXIST)
   // then errorHandling function is called and response is set to ERR + respective error message
   char path[] = "/var/spool/mail/";
   char directory[strlen(path) + strlen(user)];
   strcpy(directory, path);
   strcat(directory, user);
   if(mkdir(directory, 777) == 0)
   {
      ///////////////////////////////////////////////////////////////////////////////
      // ->new user. needs in and out box
      char in[strlen(directory) + strlen("/in")];
      strcpy(in, directory);
      strcat(in, "/in");
      char out[strlen(directory) + strlen("/out")];
      strcpy(out, directory);
      strcat(out, "/out");
      if(mkdir(in, 777) == 0 && mkdir(out, 777) == 0)
      {
         printf("created directory %s\n", in);
         printf("created directory %s\n", out);
      }
      else
      {
         return 0;
      }
      ///////////////////////////////////////////////////////////////////////////////
      // save message file to in or out box
      char target[strlen(directory) + strlen(inOrOut)];
      strcpy(target, directory);
      strcat(target, inOrOut);
      char file[strlen(target) + strlen(subject)];
      strcpy(file, target);
      strcat(file, subject);
      ///////////////////////////////////////////////////////////////////////////////
      // if mail with subject exists already -> overwrites old message.
      // NEED : [errno 17] -> File exists, needed?
      FILE* messageFile = fopen(file, "w");
      if(messageFile != NULL)
      {
         fputs("from: ", messageFile);
         fputs(sender, messageFile);
         fputc('\n', messageFile);
         fputs(message, messageFile);
         fputc('\n', messageFile);
         fclose(messageFile);
         return 1;
      }
      else
      {
         errorHandling(errno);
         return 0;
      }
      
   }
   else if(errno == EEXIST)
   { 
      ///////////////////////////////////////////////////////////////////////////////
      // user exists -> has in and out box -> just need to save message
      char target[strlen(directory) + strlen(inOrOut)];
      strcpy(target, directory);
      strcat(target, inOrOut);
      char file[strlen(target) + strlen(subject)];
      strcpy(file, target);
      strcat(file, subject);
      ///////////////////////////////////////////////////////////////////////////////
      // if mail with subject exists already -> overwrites old message
      FILE* messageFile = fopen(file, "w");
      if(messageFile != NULL)
      {
         fputs("from: ", messageFile);
         fputs(sender, messageFile);
         fputc('\n', messageFile);
         fputs(message, messageFile);
         fputc('\n', messageFile);
         fclose(messageFile);
         return 1;
      }
      else
      {
         errorHandling(errno);
         return 0;
      }
   }
   else
   {
      ///////////////////////////////////////////////////////////////////////////////
      // error handling other errnos
      errorHandling(errno);
      return 0;
   }
}

void listMail(char* username)
{
   ///////////////////////////////////////////////////////////////////////////////
   // path to user directory is arranged with /var/spool/mail/in/<username>
   // if directory does not exist, user does not exist, hence user has no messages
   // so if opendir(directory) == NULL >> response is set to "There are 0 messages..."
   char path[] = "/var/spool/mail/";
   char directory[strlen(path) + strlen(username) + strlen("/in")];
   strcpy(directory, path);
   strcat(directory, username);
   strcat(directory, "/in");
   struct dirent *dir;
   DIR *dr = opendir(directory);
   if(dr == NULL)
   {
      errorHandling(errno);
      strcat(response, "There are 0 messages for user ");
      strcat(response, username);
      strcat(response, ".\n");
      return;
   }
   ///////////////////////////////////////////////////////////////////////////////
   //iterate once through directory to get messagecount
   char buffer [sizeof(int)*8+1];
   int counter = 0;
   while((dir = readdir(dr)) != NULL)
   {
      if(strcmp(dir->d_name, ".") && strcmp(dir->d_name, "..")){
         ++counter;
      }
   }
   if(counter == 1)
   {
      //printf("There is %d message for this user.\n", counter);
      strcpy(response, "There is 1 message for user ");
      strcat(response, username);
      strcat(response, ".\n");
   }
   else
   {
      sprintf (buffer, "%d", counter);
      //printf("There are %d messages for this user.\n", counter);
      strcpy(response, "There are ");
      strcat(response, buffer);
      strcat(response, " messages for user ");
      strcat(response, username);
      strcat(response, ".\n");
      //printf("%s\n", response);
   }
   counter = 1;
   closedir(dr);

   ///////////////////////////////////////////////////////////////////////////////
   // iterate again through directory to set response to a list of message numbers and subjects
   // if we had a lot of messages that don't fit in the 1024 sized char array
   // to prevent a buffer overflow -> display message count but not all messages
   dr = opendir(directory);
   while((dir = readdir(dr)) != NULL)
   {
      if(strcmp(dir->d_name, ".") && strcmp(dir->d_name, ".."))
      {
         //printf("%d: %s\n", counter, dir->d_name);
         sprintf(buffer, "%d", counter);
         if((strlen(response)+strlen(buffer)) > (BUF-2))
         {
            break;
         }
         strcat(response, buffer);
         if((strlen(response)+strlen(": ")) > (BUF-2))
         {
            break;
         }
         strcat(response, ": ");
         if((strlen(response)+strlen(dir->d_name)) > (BUF-2))
         {
            break;
         }
         strcat(response, dir->d_name);
         if((strlen(response)+strlen("\n")) > (BUF-2))
         {
            break;
         }
         strcat(response, "\n");
         ++counter;
      }
   }
   //printf("%s\n", response); 
   closedir(dr);
}

void readMail(char* username, int msgnumber)
{
   ///////////////////////////////////////////////////////////////////////////////
   // again path to user directory is arranged with /var/spool/mail/in/<username>
   // if directory cannot be opened, we call errorHandling
   char path[] = "/var/spool/mail/";
   char directory[strlen(path) + strlen(username) + strlen("/in")];
   strcpy(directory, path);
   strcat(directory, username);
   strcat(directory, "/in");
   struct dirent *dir;
   DIR *dr = opendir(directory);
   if(dr == NULL)
   {
      errorHandling(errno);
      strcpy(response, "ERR - does not exist.\n");
      return;
   }
   ///////////////////////////////////////////////////////////////////////////////
   // iterate once through directory to get the message with the correct number
   // when counter and msgnumber are equal, message was found
   // if msgnumber is not equal to the counter after the iteration through the directory
   // is finished, the message did not exist. Either is was a negative number
   // or too big
   int counter = 0;
   while((dir = readdir(dr)) != NULL)
   {
      if(strcmp(dir->d_name, ".") && strcmp(dir->d_name, ".."))
      {
         //printf("%d: %s\n", counter, dir->d_name);
         ++counter;
         if(counter == msgnumber)
         {
            break;
         } 
      }
   } 
   closedir(dr);
   if(msgnumber != counter)
   {
      strcpy(response, "ERR\nThis message does not exist\n");
   }
   ///////////////////////////////////////////////////////////////////////////////
   // dir->d_name now points to respective message if it is not NULL
   // and counter is euqal to messagenumber -> open and display message
   // again if message cannot be opened for any reason -> set error message
   // in errorHandling
   else if(dir != NULL && counter == msgnumber)
   {
      char file[strlen(directory) + strlen("/") + strlen(dir->d_name)];
      strcpy(file, directory);
      strcat(file, "/");
      strcat(file, dir->d_name);
      FILE* messageFile = fopen(file, "r");
      if(messageFile != NULL)
      {
         char buffer[BUF];
         strcpy(response, "OK\n");
         while(fgets(buffer, BUF, messageFile) != NULL)
         {
            strcat(response, buffer);
            //printf("%s\n", buffer);
         }
         fclose(messageFile);
      }
      else
      {
         errorHandling(errno);
      }
   }
}

void deleteMail(char* username, int msgnumber)
{
   ///////////////////////////////////////////////////////////////////////////////
   // same logic as in read but with remove() when respective message was found
   // printf("username: %s\nmessagenumber: %d\n", username, msgnumber);
   char path[] = "/var/spool/mail/";
   char directory[strlen(path) + strlen(username) + strlen("/in")];
   strcpy(directory, path);
   strcat(directory, username);
   strcat(directory, "/in");
   struct dirent *dir;
   DIR *dr = opendir(directory);
   if(dr == NULL)
   {
      errorHandling(errno);
      printf("There are 0 messages for user %s.\n", username);
   }
   int counter = 1;
   while((dir = readdir(dr)) != NULL)
   {
      if(strcmp(dir->d_name, ".") && strcmp(dir->d_name, ".."))
      {
         //printf("%d: %s\n", counter, dir->d_name);
         if(counter == msgnumber)
         {
               break;
         }
         ++counter;
      }
   } 
   closedir(dr);
   ///////////////////////////////////////////////////////////////////////////////
   //dir->d_name now points to respective message -> open and display message:
   if(dir != NULL)
   {
      char file[strlen(directory) + strlen("/") + strlen(dir->d_name)];
      strcpy(file, directory);
      strcat(file, "/");
      strcat(file, dir->d_name);
      if(remove(file) == 0)
      {
         printf("removed %s successfully\n", file);
         strcpy(response, "OK\n");
      }
      else
      {
         errorHandling(errno);
      }
   }
   else
   {
      strcpy(response, "ERR - could not remove message\n");
   }
}

   ///////////////////////////////////////////////////////////////////////////////
   // errorHandling is called after operations which set errno have failed
   // the cases reflect those errnos which might occur accoridng to the function's man pages
void errorHandling(int error)
{
   switch(error)
   {
      case EACCES:
         strcpy(response, "ERR - permission denied\n");
         break;
      case EBADF:
         strcpy(response, "ERR - bad file number\n");
         break;
      case EMFILE:
         strcpy(response, "ERR - too many open files\n");
         break;
      case ENFILE:
         strcpy(response, "ERR - file table oveflow\n");
         break;
      case ENOENT:
         strcpy(response, "ERR - no such file or directory\n");
         break;
      case ENOMEM:
         strcpy(response, "ERR - not enough core\n");
         break;
      case ENOTDIR:
         strcpy(response, "ERR - not a directory\n");
         break;
      case EBUSY:
         strcpy(response, "ERR - mount device busy\n");
         break;
      case EFAULT:
         strcpy(response, "ERR - bad address\n");
         break;
      case EIO:
         strcpy(response, "ERR - I/O error\n");
         break;
      case EISDIR:
         strcpy(response, "ERR - is a directory\n");
         break;
      case ELOOP:
         strcpy(response, "ERR - symbolic link loop\n");
         break;
      case ENAMETOOLONG:
         strcpy(response, "ERR - path name is too long\n");
         break;
      case EPERM:
         strcpy(response, "ERR - not super-user\n");
         break;
      case EROFS:
         strcpy(response, "ERR - read only file system\n");
         break;
      case EINVAL:
         strcpy(response, "ERR - invalid argument\n");
         break;
      case ENOTEMPTY:
         strcpy(response, "ERR - directory not empty\n");
         break;
      case EDQUOT:
         strcpy(response, "ERR - disc quota exceeded\n");
         break;
      case EEXIST:
         strcpy(response, "ERR - file exists\n");
         break;
      case EFBIG:
         strcpy(response, "ERR - file too large\n");
         break;
      case EINTR:
         strcpy(response, "ERR - interrupted system call\n");
         break;
      case ENODEV:
         strcpy(response, "ERR - no such device\n");
         break;
      case ENOSPC:
         strcpy(response, "ERR - no space left on device\n");
         break;
      case ENXIO:
         strcpy(response, "ERR - no such device or address\n");
         break;
      case EOPNOTSUPP:
         strcpy(response, "ERR - operation not supported\n");
         break;
      case EOVERFLOW:
         strcpy(response, "ERR - value too large to be stored in data type\n");
         break;
      case ETXTBSY:
         strcpy(response, "ERR - text file busy\n");
         break;
      case EWOULDBLOCK:
         strcpy(response, "ERR - resource temporarily unavailabe\n");
         break;
      default: strcpy(response, "ERR - unknown error\n");
   }
}

int ldapCredentials(char* fulluid, char* searchedUid, char* pwd)
{
   ////////////////////////////////////////////////////////////////////////////
   // setup LDAP connection
   // https://linux.die.net/man/3/ldap_initialize
   printf("fulliud: %s\nsearchedUid: %s\n", fulluid, searchedUid);
   LDAP *ld;
   int l = ldap_initialize(&ld, "ldap://ldap.technikum-wien.at:389");
   if(l != LDAP_OPT_SUCCESS)
   {
      printf("%s\n", ldap_err2string(l));
      perror("ldap_initialize - Error: ");
      return 0;
   }

   ////////////////////////////////////////////////////////////////////////////
   // set verison options
   // https://linux.die.net/man/3/ldap_set_option
   int ldapVersion = LDAP_VERSION3;

   l = ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &ldapVersion);
   if (l != LDAP_OPT_SUCCESS)
   {
      printf("%s\n", ldap_err2string(l));
      perror("ldap_start_tls_s - Error: ");
      ldap_unbind_ext_s(ld, NULL, NULL);
      return 0;
   }

   ////////////////////////////////////////////////////////////////////////////
   // start connection secure (initialize TLS)
   // https://linux.die.net/man/3/ldap_start_tls_s
   // int ldap_start_tls_s(LDAP *ld,
   //                      LDAPControl **serverctrls,
   //                      LDAPControl **clientctrls);
   // https://linux.die.net/man/3/ldap
   // https://docs.oracle.com/cd/E19957-01/817-6707/controls.html
   //    The LDAPv3, as documented in RFC 2251 - Lightweight Directory Access
   //    Protocol (v3) (http://www.faqs.org/rfcs/rfc2251.html), allows clients
   //    and servers to use controls as a mechanism for extending an LDAP
   //    operation. A control is a way to specify additional information as
   //    part of a request and a response. For example, a client can send a
   //    control to a server as part of a search request to indicate that the
   //    server should sort the search results before sending the results back
   //    to the client.
   l = ldap_start_tls_s(ld, NULL, NULL);
   if (l != LDAP_SUCCESS)
   {
      printf("%s\n", ldap_err2string(l));
      perror("ldap_start_tls_s - Error: ");
      ldap_unbind_ext_s(ld, NULL, NULL);
      return 0;
   }

   ////////////////////////////////////////////////////////////////////////////
   // bind credentials
   // https://linux.die.net/man/3/lber-types
   // SASL (Simple Authentication and Security Layer)
   // https://linux.die.net/man/3/ldap_sasl_bind_s
   // int ldap_sasl_bind_s(
   //       LDAP *ld,
   //       const char *dn,
   //       const char *mechanism,
   //       struct berval *cred,
   //       LDAPControl *sctrls[],
   //       LDAPControl *cctrls[],
   //       struct berval **servercredp);
   BerValue bindCredentials; //BerValue = public class // storage units of a variety of sizes
   bindCredentials.bv_val = (char *)pwd; //if bv is NULL, routine does nothing
   bindCredentials.bv_len = strlen(pwd);
   BerValue *servercredp;
   l = ldap_sasl_bind_s(ld, fulluid, LDAP_SASL_SIMPLE, &bindCredentials, NULL, NULL, &servercredp);
   if(l != LDAP_SUCCESS)
   {
      printf("%s\n", ldap_err2string(l));
      perror("ldap_sasl_bind_s - Error: ");
      ldap_unbind_ext_s(ld, NULL, NULL);
      return 0;
   }

   // search settings
   const char* base = "dc=technikum-wien,dc=at"; // search base
   ber_int_t scope = LDAP_SCOPE_SUBTREE; 
   char filter[128] = "(uid=";
   strcat(filter, searchedUid);
   strcat(filter, "*)\0");
   LDAPMessage *res;

   ////////////////////////////////////////////////////////////////////////////
   // perform ldap search
   // https://linux.die.net/man/3/ldap_search_ext_s
   // _s : synchronous
   // int ldap_search_ext_s(
   //     LDAP *ld,
   //     char *base,
   //     int scope,
   //     char *filter,
   //     char *attrs[],
   //     int attrsonly,
   //     LDAPControl **serverctrls,
   //     LDAPControl **clientctrls,
   //     struct timeval *timeout,
   //     int sizelimit,
   //     LDAPMessage **res );
   l = ldap_search_ext_s(ld, base, scope, filter, NULL, 0, NULL, NULL, NULL, 500, &res);
   if(l != LDAP_SUCCESS)
   {
      printf("%s\n", ldap_err2string(l));
      perror("ldap_search_ext_s - Error: ");
      ldap_unbind_ext_s(ld, NULL, NULL);
      return 0;
   }

   int resultCount = ldap_count_entries(ld, res);
   printf("Total results for searched uid %s: %d\n", searchedUid, resultCount);

   // free memory
   ldap_msgfree(res);

   ////////////////////////////////////////////////////////////////////////////
   // https://linux.die.net/man/3/ldap_unbind_ext_s
   // int ldap_unbind_ext_s(
   //       LDAP *ld,
   //       LDAPControl *sctrls[],
   //       LDAPControl *cctrls[]);
   ldap_unbind_ext_s(ld, NULL, NULL);
   return resultCount; // i.e. 1 if person found, 0 if not
   
}