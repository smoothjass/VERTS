///////////////////////////////////////////////////////////////////////////////
   ///////////////////////////////////////////////////////////////////////////////
   //                                                                           //
   // myfind winter term 2022                                                   //
   //                                                                           //
   // authors:                                                                  //
   // Jasmin Duvivié if21b145@technikum-wien.at                                 //
   // Si Si Sun if21b102@technikum-wien.at                                      //
   //                                                                           //
   ///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////INCLUDES
# include <stdio.h>
# include <unistd.h>
# include <linux/limits.h>
# include <string.h>
# include <stdlib.h>
# include <dirent.h>
# include <errno.h>
# include <ctype.h>
# include <sys/wait.h>

///////////////////////////////////////////////////////////////////////////////MACROS
# define MAX_FILES 100 //maximum of filenames that may be searched > also maximum number of processes

///////////////////////////////////////////////////////////////////////////////PROTOTYPES
void printUsage();
int findFile(char* searchpath, char* filename, int recursive, int caseInsensitive); //returns 1 if file found and 0 if not

///////////////////////////////////////////////////////////////////////////////MAIN
int main (int argc, char* argv[]){
    if (argc < 3) {
        printf("Too few arguments\n");
        printUsage();
        return 0;
    }

    int recursive = 0;
    int caseInsensitive = 0;
    
    int ancillary = 0;
    int found = 0;

    char searchpath[PATH_MAX]; //PATH_MAX from limits.h
    char filenames[MAX_FILES][NAME_MAX]; // an array that can hold a number of MAX_FILES c strings; NAME_MAX from limits.h
    int numberOfFiles = 0;

    /////////////////////////////
    // parse options with getopt
    while ((ancillary = getopt(argc, argv, "Ri")) != -1 ) {
        switch (ancillary) {
            case 'R':
                recursive = 1;
                break;
            case 'i':
                caseInsensitive = 1;
                break;
            default:
                printUsage();
                return 0;
        }
    }

    //////////////////////////////////////////////////////////////////
    // parse arguments (i.e. searchpaths and filename(s)) with optind
    if (!(optind < argc)) {
        printf("searchpath and filename(s) required\n");
        printUsage();
        return 0;
    }
    if (optind < argc) {
        if (!(strlen(argv[optind]) > PATH_MAX)) { // to prevent buffer overflow
            strcpy(searchpath, argv[optind]);
            ++optind;
        }
        if (!(optind < argc)) {
            printf("filename(s) required\n");
            printUsage();
            return 0;
        }
        for (int i = 0; i < MAX_FILES && optind < argc; ++i) {
            if (!(strlen(argv[optind]) > NAME_MAX)) {
                strcpy(filenames[i], argv[optind]);
                ++optind;
                ++numberOfFiles;
            }
        }
    }
    
    ////////////////////////////////////
    // summary of the search parameters
    printf("\nSearching for the following files:\n");
    for (int i = 0; i < MAX_FILES && i < numberOfFiles; ++i) {
        printf("%s\n", filenames[i]);
    }
    printf("\nin directory:\n%s\n\n", searchpath);
    if (recursive) {
        printf("in recursive mode\n\n");
    }
    if (caseInsensitive) {
        printf("without case sensitivity\n\n");
    }

    ////////////////////////////////
    // fork child for each filename
    pid_t pid;
    
    for (int i = 0; i < numberOfFiles; ++i) {
        pid = fork();
        switch(pid) {
            case -1: // error while forking
                printf("Could not fork()\n");
                return EXIT_FAILURE;
                break;
            case 0: // child
                found = findFile(searchpath, filenames[i], recursive, caseInsensitive);
                if (!found) {
                    printf("%d: %s: not found\n", getpid(), filenames[i]);
                }
                break;
            default: // parent
                break;
        }
        if(pid == 0){ // break if child
            break;
        }
    }

    while(wait(NULL) > 0); // parent waits for children
    return 0;
}
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////FUNCTIONS
void printUsage() {
    printf("Usage: ./myfind [-R] [-i] searchpath filename1 [filename2] …[filenameN]\n");
}

int findFile (char* searchpath, char* filename, int recursive, int caseInsensitive) {
    int found = 0;

    struct dirent *direntp;
    DIR *dirp;

    if ((dirp = opendir(searchpath)) == NULL) { // open directory
        perror("Failed to open directory");
        return 0;
    }

    while ((direntp = readdir(dirp)) != NULL) {
        if (caseInsensitive) { 
            if (strcasecmp(direntp->d_name, filename) == 0) { // strings are compared without case sensitivity
                printf("%d: %s: %s/%s\n", getpid(), filename, searchpath, direntp->d_name);
                found = 1;
                break;
            }
        }
        else if (strcmp(direntp->d_name, filename) == 0) { // strings are compared with case sensitivity
            printf("%d: %s: %s/%s\n", getpid(), filename, searchpath, direntp->d_name);
            found = 1;
            break;
        }
        ////////////////////////////////////////////////////////////////////////
        // in recursive mode:
        // check for and enter directories (except wd . and directory above ..)
        // recursively call findFile with the new temporary searchpath
        if (recursive && direntp->d_type == DT_DIR && strcmp(direntp->d_name, ".") != 0 && strcmp(direntp->d_name, "..") != 0) {
            char tempPath[(strlen(searchpath) + strlen("/") + strlen(direntp->d_name))];
            strcpy(tempPath, searchpath);
            strcat(tempPath, "/");
            strcat(tempPath, direntp->d_name);
            //printf("\ndebug: tempPath: %s\n", tempPath);
            found = findFile(tempPath, filename, recursive, caseInsensitive);
        }
    }
    while ((closedir(dirp) == -1) && (errno == EINTR)); // close directory
    return found;
}
///////////////////////////////////////////////////////////////////////////////