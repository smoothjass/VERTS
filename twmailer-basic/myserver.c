#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <dirent.h>

///////////////////////////////////////////////////////////////////////////////
   ///////////////////////////////////////////////////////////////////////////////
   //
   // TWMailer Basic server winter term 2022
   //
   // authors:
   // Jasmin Duvivié if21b145@technikum-wien.at
   // Si Si Sun if21b102@technikum-wien.at
   // whoever made the clientServerSample in moodle (Daniel Kienboeck?)
   //
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
      // START CLIENT
      // ignore printf error handling
      printf("Client connected from %s:%d...\n",
             inet_ntoa(cliaddress.sin_addr),
             ntohs(cliaddress.sin_port));
      clientCommunication(&new_socket); // returnValue can be ignored
      new_socket = -1;
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

   return EXIT_SUCCESS;
}

void *clientCommunication(void *data)
{
   char buffer[BUF];
   int size;
   int *current_socket = (int *)data;

   ////////////////////////////////////////////////////////////////////////////
   // SEND welcome message
   strcpy(buffer, "Welcome to myserver!\r\nPlease enter your commands...\r\n");
   if (send(*current_socket, buffer, strlen(buffer), 0) == -1)
   {
      perror("send failed");
      return NULL;
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
      // segmentation fault -> TO-DO for TWMailer Pro! fix this!
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
      char username[strlen(token)];
      
      char sender[strlen(token)];
      char receiver[BUF];
      char subject[BUF];
      char message[BUF];

      int msgnumber = 0;

      switch (type){
         case sendMessage:
            ///////////////////////////////////////////////////////////////////////////////
            // parse: sender, receiver, subject, message
            strcpy(sender, token);
            token = strtok(NULL, delimeter);
            strcpy(receiver, token);
            token = strtok(NULL, delimeter);
            strcpy(subject, token);
            token = strtok(NULL, delimeter);
            strcpy(message, token);
            int saveSuccess = saveMail(receiver, sender, subject, message, "/in/"); //save message to receivers inbox
            saveSuccess += saveMail(sender, sender, subject, message, "/out/"); //save message to senders outbox
            if(saveSuccess == 2){ // both save operations successfull
               strcpy(response, "OK\n");
            }
            else{
               strcpy(response, "ERR\n");
            }
            break;
         case listMessages:
            ///////////////////////////////////////////////////////////////////////////////
            // parse: username
            strcpy(username, token);
            listMail(username);
            break;
         case readMessage:
            ///////////////////////////////////////////////////////////////////////////////
            // read and del: parse: username, message number
            strcpy(username, token);
            token = strtok(NULL, delimeter);
            msgnumber = atoi(token);
            readMail(username, msgnumber);
            break;
         case deleteMessage:
            strcpy(username, token);
            token = strtok(NULL, delimeter);
            msgnumber = atoi(token);
            deleteMail(username, msgnumber);
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
      if (bytesSent == -1){
         perror("send answer failed");
         return NULL;
      }
      response[0] = '\0';
   } while (strcmp(buffer, "quit\n.") != 0 && !abortRequested);

   ///////////////////////////////////////////////////////////////////////////////
   // closes/frees the descriptor if not already
   if (*current_socket != -1){
      if (shutdown(*current_socket, SHUT_RDWR) == -1){
         perror("shutdown new_socket");
      }
      if (close(*current_socket) == -1){
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
      if (new_socket != -1){
         if (shutdown(new_socket, SHUT_RDWR) == -1){
            perror("shutdown new_socket");
         }
         if (close(new_socket) == -1){
            perror("close new_socket");
         }
         new_socket = -1;
      }

      if (create_socket != -1){
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

int saveMail(char* user, char* sender, char* subject, char* message, char* inOrOut){ 
   ///////////////////////////////////////////////////////////////////////////////
   // inOrOut must be "/in/" or "/out/"
   // depending if the message should be persisted in the inbox of receiver or the outbox of sender

   ///////////////////////////////////////////////////////////////////////////////
   // path to user directory is arranged with /var/spool/mail/<username>
   // if it does not exist already (mkdir(directory, 777) == 0), in and outbox must be created
   // if user does exist (errno == EEXIST), message is persisted in repsective subdirectory in or out
   // if a message with the same subject exists already in respective directory
   // then it is overwritten -> TO-DO for TWMailer Pro: fix this!
   // if user directory could not be created to any other reasons (i.e. mkdir(directory, 777) != 0 but errno != EEXIST)
   // then errorHandling function is called and response is set to ERR + respective error message
   char path[] = "/var/spool/mail/";
   char directory[strlen(path) + strlen(user)];
   strcpy(directory, path);
   strcat(directory, user);
   if(mkdir(directory, 777) == 0){
      ///////////////////////////////////////////////////////////////////////////////
      // ->new user. needs in and out box
      char in[strlen(directory) + strlen("/in")];
      strcpy(in, directory);
      strcat(in, "/in");
      char out[strlen(directory) + strlen("/out")];
      strcpy(out, directory);
      strcat(out, "/out");
      if(mkdir(in, 777) == 0 && mkdir(out, 777) == 0){
         printf("created directory %s\n", in);
         printf("created directory %s\n", out);
      }
      else{
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
      // maybe TO-DO for TWMailer pro (errno 17)
      FILE* messageFile = fopen(file, "w");
      if(messageFile != NULL){
         fputs("from: ", messageFile);
         fputs(sender, messageFile);
         fputc('\n', messageFile);
         fputs(message, messageFile);
         fputc('\n', messageFile);
         fclose(messageFile);
         return 1;
      }
      else{
         errorHandling(errno);
         return 0;
      }
      
   }
   else if(errno == EEXIST){ 
      ///////////////////////////////////////////////////////////////////////////////
      // user exists -> has in and out box -> just need to save message
      char target[strlen(directory) + strlen(inOrOut)];
      strcpy(target, directory);
      strcat(target, inOrOut);
      char file[strlen(target) + strlen(subject)];
      strcpy(file, target);
      strcat(file, subject);
      ///////////////////////////////////////////////////////////////////////////////
      // if mail with subject exists already -> overwrites old message. maybe TO-DO for TWMailer pro (errno 17)
      FILE* messageFile = fopen(file, "w");
      if(messageFile != NULL){
         fputs("from: ", messageFile);
         fputs(sender, messageFile);
         fputc('\n', messageFile);
         fputs(message, messageFile);
         fputc('\n', messageFile);
         fclose(messageFile);
         return 1;
      }
      else{
         errorHandling(errno);
         return 0;
      }
   }
   else{
      ///////////////////////////////////////////////////////////////////////////////
      // error handling other errnos
      errorHandling(errno);
      return 0;
   }
}

void listMail(char* username){
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
   if(dr == NULL){
      errorHandling(errno);
      strcat(response, "There are 0 messages for this user.\n");
      return;
   }
   ///////////////////////////////////////////////////////////////////////////////
   //iterate once through directory to get messagecount
   char buffer [sizeof(int)*8+1];
   int counter = 0;
   while((dir = readdir(dr)) != NULL){
      if(strcmp(dir->d_name, ".") && strcmp(dir->d_name, "..")){
         ++counter;
      }
   }
   if(counter == 1){
      //printf("There is %d message for this user.\n", counter);
      strcpy(response, "There is 1 message for this user.\n");
   }
   else{
      sprintf (buffer, "%d", counter);
      //printf("There are %d messages for this user.\n", counter);
      strcpy(response, "There are ");
      strcat(response, buffer);
      strcat(response, " messages for this user.\n");
      //printf("%s\n", response);
   }
   counter = 1;
   closedir(dr);

   ///////////////////////////////////////////////////////////////////////////////
   // iterate again through directory to set response to a list of message numbers and subjects
   // not entirely sure what happens, if we have a lot messages that don't fit in the 1024 sized char array
   // probably a segmentation fault >> TO-DO for TWMailer Pro: check and maybe fix this
   dr = opendir(directory);
   while((dir = readdir(dr)) != NULL){
      if(strcmp(dir->d_name, ".") && strcmp(dir->d_name, "..")){
         //printf("%d: %s\n", counter, dir->d_name);
         sprintf(buffer, "%d", counter);
         strcat(response, buffer);
         strcat(response, ": ");
         strcat(response, dir->d_name);
         strcat(response, "\n");
         ++counter;
      }
   }
   //printf("%s\n", response); 
   closedir(dr);
}

void readMail(char* username, int msgnumber){
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
   if(dr == NULL){
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
   while((dir = readdir(dr)) != NULL){
      if(strcmp(dir->d_name, ".") && strcmp(dir->d_name, "..")){
         //printf("%d: %s\n", counter, dir->d_name);
         ++counter;
         if(counter == msgnumber){
               break;
         } 
      }
   } 
   closedir(dr);
   if(msgnumber != counter){
      strcpy(response, "ERR\nThis message does not exist\n");
   }
   ///////////////////////////////////////////////////////////////////////////////
   // dir->d_name now points to respective message if it is not NULL
   // and counter is euqal to messagenumber -> open and display message
   // again if message cannot be opened for any reason -> set error message
   // in errorHandling
   else if(dir != NULL && counter == msgnumber){
      char file[strlen(directory) + strlen("/") + strlen(dir->d_name)];
      strcpy(file, directory);
      strcat(file, "/");
      strcat(file, dir->d_name);
      FILE* messageFile = fopen(file, "r");
      if(messageFile != NULL){
         char buffer[BUF];
         strcpy(response, "OK\n");
         while(fgets(buffer, BUF, messageFile) != NULL){
            strcat(response, buffer);
            //printf("%s\n", buffer);
         }
         fclose(messageFile);
      }
      else{
         errorHandling(errno);
      }
   }
}

void deleteMail(char* username, int msgnumber){
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
   if(dr == NULL){
      errorHandling(errno);
      printf("There are 0 messages for this user.\n");
   }
   int counter = 1;
   while((dir = readdir(dr)) != NULL){
      if(strcmp(dir->d_name, ".") && strcmp(dir->d_name, "..")){
         //printf("%d: %s\n", counter, dir->d_name);
         if(counter == msgnumber){
               break;
         }
         ++counter;
      }
   } 
   closedir(dr);
   ///////////////////////////////////////////////////////////////////////////////
   //dir->d_name now points to respective message -> open and display message:
   if(dir != NULL){
      char file[strlen(directory) + strlen("/") + strlen(dir->d_name)];
      strcpy(file, directory);
      strcat(file, "/");
      strcat(file, dir->d_name);
      if(remove(file) == 0){
         printf("removed %s successfully\n", file);
         strcpy(response, "OK\n");
      }
      else{
         errorHandling(errno);
      }
   }
   else{
      strcpy(response, "ERR - could not remove message\n");
   }
}

   ///////////////////////////////////////////////////////////////////////////////
   // errorHandling is called after operations which set errno have failed
   // the cases reflect those errnos which might occur accoridng to the function's man pages
void errorHandling(int error){
   switch(error){
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