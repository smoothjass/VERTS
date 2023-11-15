#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <termio.h>
#include <iostream>

///////////////////////////////////////////////////////////////////////////////
   ///////////////////////////////////////////////////////////////////////////////
   //                                                                           //
   // TWMailer Pro client winter term 2022                                      //    
   //                                                                           //
   // authors:                                                                  //
   // Jasmin Duvivié if21b145@technikum-wien.at                                 //
   // Si Si Sun if21b102@technikum-wien.at                                      //
   // whoever made the samples in moodle (Daniel Kienboeck?)                    //
   //                                                                           //
   ///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////

#define BUF 1024
#define PORT 6543

const char* getpass();
///////////////////////////////////////////////////////////////////////////////

int main(int argc, char **argv)
{
   int create_socket;
   char buffer[BUF];
   struct sockaddr_in address;
   int size;
   int messageSize = BUF-1;
   int isQuit;
   int bytesSent;

   ////////////////////////////////////////////////////////////////////////////
   // CREATE A SOCKET
   // https://man7.org/linux/man-pages/man2/socket.2.html
   // https://man7.org/linux/man-pages/man7/ip.7.html
   // https://man7.org/linux/man-pages/man7/tcp.7.html
   // IPv4, TCP (connection oriented), IP (same as server)
   if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
   {
      perror("Socket error");
      return EXIT_FAILURE;
   }

   ////////////////////////////////////////////////////////////////////////////
   // INIT ADDRESS
   // Attention: network byte order => big endian
   memset(&address, 0, sizeof(address)); // init storage with 0
   address.sin_family = AF_INET;         // IPv4
   // https://man7.org/linux/man-pages/man3/htons.3.html
   address.sin_port = htons(PORT);
   // https://man7.org/linux/man-pages/man3/inet_aton.3.html
   if (argc < 2)
   {
      inet_aton("127.0.0.1", &address.sin_addr);
   }
   else
   {
      inet_aton(argv[1], &address.sin_addr);
   }

   ////////////////////////////////////////////////////////////////////////////
   // CREATE A CONNECTION
   // https://man7.org/linux/man-pages/man2/connect.2.html
   if (connect(create_socket,
               (struct sockaddr *)&address,
               sizeof(address)) == -1)
               {
      // https://man7.org/linux/man-pages/man3/perror.3.html
      perror("Connect error - no server available");
      return EXIT_FAILURE;
   }
   ///////////////////////////////////////////////////////////////////////////////
   // ignore return value of printf
   printf("Connection with server (%s) established\n",
          inet_ntoa(address.sin_addr));

   ////////////////////////////////////////////////////////////////////////////
   // RECEIVE DATA
   // https://man7.org/linux/man-pages/man2/recv.2.html
   size = recv(create_socket, buffer, BUF - 1, 0);
   //printf("size: %d\n", size);
   if (size == -1)
   {
      perror("recv error");
   }
   else if (size == 0)
   {
      printf("Server closed remote socket\n"); // ignore error
   }
   else
   {
      buffer[size] = '\0';
      printf("%s", buffer); // ignore error
   }
   ///////////////////////////////////////////////////////////////////////////////
   // CHECK USER ID & PW
   int loginSuccess = false;
   int tries = 0;

   while(!loginSuccess)
   {
      printf("LOGIN REQUIRED\nEnter uid: ");
      char rawuid[128];
      fgets(rawuid,127,stdin);
      rawuid[strcspn(rawuid, "\n")] = 0; //looks for index of new line and makes it 0
      //https://stackoverflow.com/questions/2693776/removing-trailing-newline-character-from-fgets-input
      //printf("Enter pw: ");
      char pwd[256];
      strcpy(pwd, getpass());
      bytesSent = send(create_socket, rawuid, messageSize, 0);
      if(bytesSent == -1)
      {
         perror("send error");
         break;
      }
      bytesSent = send(create_socket, pwd, messageSize, 0);
      if(bytesSent == -1)
      {
         perror("send error");
         break;
      }
      size = recv(create_socket, buffer, messageSize, 0);
      //printf("size: %d\n", size);
      if (size == -1)
      {
         printf("recv size = -1");
         perror("recv error");
         break;
      }
      else if (size == 0)
      {
         printf("Server closed remote socket\n"); // ignore error
         break;
      }
      else
      {
         buffer[size] = '\0';
         printf("<< %s\n", buffer); // ignore error
         if(strcmp(buffer, "LOGINOK") == 0)
         {
            loginSuccess = true;
            break;
         }
         else
         {
            printf("Invalid credentials\n");
            if(tries == 2){
               printf("Too many tries. Wait a minute to try again!\n");
               //sleep(5); //fürs debuggen
               sleep(60);
               tries = 0;
            }
         }
         ++tries;
      }
      //printf("wtf\n"); //debug message
   }

   //printf("rawuid: %s\nfulluid: %s\npw: %s\n", rawuid, fulluid, pwd); //test
   do
   {
      ///////////////////////////////////////////////////////////////////////////////
      // loop to get characters until .\n is entered (end of request as defined in the task)
      // or until maximum messagesize (BUF - 1) is reached
      for(int i = 0; i < messageSize; ++i)
      {
         buffer[i] = fgetc(stdin);
         if(buffer[i] == '\n' && !feof(stdin))
         {
            if(i > 0)
            {
               if(buffer[i-1] == '.')
               {
                  buffer[i] = '\0'; //string ends here
                  break;
               }
            }
         }
      }
      
      if(buffer != NULL)
      {
      
         isQuit = strcmp(buffer, "quit\n.") == 0;

         //////////////////////////////////////////////////////////////////////
         // SEND DATA
         // https://man7.org/linux/man-pages/man2/send.2.html
         // send will fail if connection is closed, but does not set
         // the error of send, but still the count of bytes sent
         bytesSent = send(create_socket, buffer, messageSize, 0);
         //printf("bytes sent: %d\n", bytesSent); //ab dem zweiten kommando: bytes sent: 3 >> Fehler im client
         if (bytesSent == -1) 
         {
            ///////////////////////////////////////////////////////////////////////////////
            // in case the server is gone offline we will still not enter
            // this part of code: see docs: https://linux.die.net/man/3/send
            // >> Successful completion of a call to send() does not guarantee 
            // >> delivery of the message. A return value of -1 indicates only 
            // >> locally-detected errors.
            // ... but
            // to check the connection before send is sense-less because
            // after checking the communication can fail (so we would need
            // to have 1 atomic operation to check...)
            perror("send error");
            break;
         }

         //////////////////////////////////////////////////////////////////////
         // RECEIVE FEEDBACK
         // consider: reconnect handling might be appropriate in somes cases
         //           How can we determine that the command sent was received 
         //           or not? 
         //           - Resend, might change state too often. 
         //           - Else a command might have been lost.
         //
         // solution 1: adding meta-data (unique command id) and check on the
         //             server if already processed.
         // solution 2: add an infrastructure component for messaging (broker)
         //
         size = recv(create_socket, buffer, messageSize, 0);
         //printf("size: %d\n", size);
         if (size == -1)
         {
            perror("recv error");
            break;
         }
         else if (size == 0)
         {
            printf("Server closed remote socket\n"); // ignore error
            break;
         }
         else
         {
            buffer[size] = '\0';
            printf("<< %s\n", buffer); // ignore error
         }
      }
   } while (!isQuit);

   ////////////////////////////////////////////////////////////////////////////
   // CLOSES THE DESCRIPTOR
   if (create_socket != -1)
   {
      if (shutdown(create_socket, SHUT_RDWR) == -1)
      {
         // invalid in case the server is gone already
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

int getch()
{
    int ch;
    // https://man7.org/linux/man-pages/man3/termios.3.html
    struct termios t_old, t_new;

    // https://man7.org/linux/man-pages/man3/termios.3.html
    // tcgetattr() gets the parameters associated with the object referred
    //   by fd and stores them in the termios structure referenced by
    //   termios_p
    tcgetattr(STDIN_FILENO, &t_old);
    
    // copy old to new to have a base for setting c_lflags
    t_new = t_old;

    // https://man7.org/linux/man-pages/man3/termios.3.html
    //
    // ICANON Enable canonical mode (described below).
    //   * Input is made available line by line (max 4096 chars).
    //   * In noncanonical mode input is available immediately.
    //
    // ECHO   Echo input characters.
    t_new.c_lflag &= ~(ICANON | ECHO);
    
    // sets the attributes
    // TCSANOW: the change occurs immediately.
    tcsetattr(STDIN_FILENO, TCSANOW, &t_new);

    ch = getchar();

    // reset stored attributes
    tcsetattr(STDIN_FILENO, TCSANOW, &t_old);

    return ch;
}

const char *getpass()
{
    int show_asterisk = 0;

    const char BACKSPACE = 127;
    const char RETURN = 10;

    unsigned char ch = 0;
    std::string password;

    printf("Password: ");

    while ((ch = getch()) != RETURN)
    {
        if (ch == BACKSPACE)
        {
            if (password.length() != 0)
            {
                if (show_asterisk)
                {
                    printf("\b \b"); // backslash: \b
                }
                password.resize(password.length() - 1);
            }
        }
        else
        {
            password += ch;
            if (show_asterisk)
            {
                printf("*");
            }
        }
    }
    printf("\n");
    return password.c_str();
}