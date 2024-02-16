#define _POSIX_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#define MAX_ARGS 512
#define BUFFER_SIZE 2048

int EXIT_STATUS = 0;
int SAVE_STDIN;
int SAVE_STDOUT;
struct sigaction SIGINT_action = {0};
struct sigaction SIGINT_fg_action = {0};
struct sigaction SIGTSTP_action = {0};
pid_t spawnpid;
int processIsBackground = 0;
int weHateBackgrounds = 0;

struct input
{
    char * command;
    char * args[MAX_ARGS];
    char * inFileName;
    int inRedirected;
    char * outFileName;
    int outRedirected;
    int backgroundProcess;
};

void handle_SIGINT(int signo) // WORKING
{
    if (processIsBackground == 0 && spawnpid != 0 && spawnpid != -40) // if the spawnpid is 0 or -40, this isn't a child process
    {
        kill(spawnpid, SIGTERM);
        fprintf(stdout, "\nProcess killed by signal %i.\n", SIGTERM);
        fflush(stdout);
    }
    else
    {
        fprintf(stdout, "\nError: No foreground process to interrupt.\n: ");
        fflush(stdout);
    }
}

void handle_SIGTSTP(int signo) // 
{
    // toggles the global flag weHateBackgrounds, which will inform parseInput() on whether to throw out &s
    if (weHateBackgrounds == 0)
    {
        weHateBackgrounds = 1;
        fprintf(stdout, "\nEntering foreground-only mode (& is now ignored)\n: ");
        fflush(stdout);
    }
    else
    {
        weHateBackgrounds = 0;
        fprintf(stdout, "\nExiting foreground-only mode\n: ");
        fflush(stdout);
    }
}

char * variableExpansion(char * inString) // WORKING
{
    int PID = getpid();
    int PIDlen = snprintf(NULL, 0, "%d", PID);
    char * PIDstring = (char*)calloc(PIDlen + 1, sizeof(char)); 
    snprintf(PIDstring, PIDlen + 1, "%d", PID); // create a string that's actually the PID
    char * buffer = (char*)calloc(BUFFER_SIZE, sizeof(char));
    int inputCounter = 0;
    int bufferCounter = 0;
    //	what we have is inString, PIDstring, and buffer. we want to look through inString for a $$, and if we find it,
    //	instead of adding in the $$ we want to add the entirety of PIDstring.
    while(inputCounter <= strlen(inString))
    {
        if(inString[inputCounter+1])	// check if the next index exists
        {
            if(inString[inputCounter] == '$' && inString[inputCounter+1] == '$')  // if it does, and current character and next character are $
            {
                inputCounter = inputCounter + 2;    // skip i forward two indices
                for(int k = 0; k < PIDlen; ++k)
                {
                    if(PIDstring[k] != '\0')
                    {
                        buffer[bufferCounter] = PIDstring[k];   // set next K indices of buffer to PIDstring, hopefully offsetting I and J the right amount
                        ++bufferCounter;
                    }
                }
            }
        }
        if(inString[inputCounter] == '\0')
        {
            buffer[bufferCounter] = inString[inputCounter];	// grab the last character
            inputCounter = BUFFER_SIZE;    // we're done, escape the for loop
        }
        else
        {
            buffer[bufferCounter] = inString[inputCounter];
            ++bufferCounter;
            ++inputCounter;
        }
    }
    char * outString = (char*)calloc(strlen(buffer), sizeof(char));
    strcpy(outString, buffer);
    free(buffer);
    free(PIDstring);
    return outString;
}

void parseInput(struct input *userInput) // WORKING, I THINK
{
    char * inBuffer;
    inBuffer = (char*)calloc(BUFFER_SIZE, sizeof(char));  // allocate the buffer memory
    fprintf(stdout, ": ");
    fflush(stdout);
    fgets(inBuffer, BUFFER_SIZE, stdin);
    int argCount = 1;	// start at arg[1], since arg[0] == command
    char delim[] = " ";
    char * currentWord = strtok(inBuffer, delim);  // read in the current word
    currentWord = variableExpansion(currentWord);
    userInput->command = currentWord; // word #0 is always the command
    userInput->args[0] = (char*)calloc(strlen(userInput->command) + 1, sizeof(char));
    strcpy(userInput->args[0], userInput->command);
    while ((currentWord = strtok(NULL, delim)) != NULL)
    {
        currentWord = variableExpansion(currentWord);

        if (strcmp(currentWord, "<") == 0)  // if we're doing input redirection,
            {
                userInput->inFileName = strtok(NULL, delim); // read the next word and make that the inFileName
                userInput->inFileName = variableExpansion(userInput->inFileName); // don't forget variable expansion
                --argCount; // preempt line 111
            }
        if (strcmp(currentWord, ">") == 0)  // if we're doing output redirection,
            {
                userInput->outFileName = strtok(NULL, delim); // do the same, but outFileName this time
                fprintf(stdout, "outFileName = %s\n", userInput->outFileName);
                fflush(stdout);
                userInput->outFileName = variableExpansion(userInput->outFileName);
                --argCount; // preempt line 111
            }
        else
        {
            userInput->args[argCount] = currentWord;
        }
        ++argCount;
    }
    if (userInput->command[strlen(userInput->command) - 1] == '\n')
    {
        userInput->command[strlen(userInput->command) - 1] = '\0'; // null terminate your strings!
    }
    if (userInput->inFileName)
    {
        if (userInput->inFileName[strlen(userInput->inFileName) - 1] == '\n')
        {
            userInput -> inFileName[strlen(userInput->inFileName) - 1] = '\0';
        }
    }
    if (userInput->outFileName)
    {
        if (userInput->outFileName[strlen(userInput->outFileName) - 1] == '\n')
        {
            userInput -> outFileName[strlen(userInput->outFileName) - 1] = '\0';
        }
    }
    if (userInput->args[argCount - 1])
    {
        char * temp = userInput->args[argCount - 1];
        if (temp[strlen(temp) - 1] == '\n')
        {
            temp[strlen(temp) - 1] = '\0';
            userInput->args[argCount - 1] = temp;
        }
    }
    if (strcmp(userInput->args[argCount - 1], "&") == 0) // if the final argument is "&", we execute in the background
    {
        userInput->backgroundProcess = 1; // set this flag and deal with it later
        if (weHateBackgrounds == 1)
        {
            userInput->backgroundProcess = 0; // un-set that flag if we're in foreground-only mode
        }
        userInput->args[argCount - 1] = NULL;   // get rid of the last argument, which is just &
        argCount = argCount - 1;
    }
    free(inBuffer); // free the buffer, we're done with it
}

void freeInputObject(struct input *victim)
{
    if(victim->command) // if command is null, everything else is, dodge a segfault
    {
        free(victim->command); // free all those char arrays we allocated in parseInput()
        free(victim->inFileName);
        free(victim->outFileName);
        for(int i = 0; i < MAX_ARGS; ++i)
        {
            if(!victim->args[i]) // if current index does not exist,
                {
                    i = MAX_ARGS; // quit lookin
                }
            else
            {
                free(victim->args[i]);
            }
        }
    }
}

int smallCD(struct input *userInput) // WORKING
{
    if (userInput->args[1]) // if arg[1] exists, it's our target directory
    {
        char * targetDirectory = (char*)calloc(BUFFER_SIZE, sizeof(char));
        strcpy(targetDirectory, userInput->args[1]);
        if (chdir(targetDirectory) != 0) // attempt to chdir, if it fails write an error message
        {
            fprintf(stderr, "Directory %s does not exist.\n", targetDirectory);
            fflush(stderr);
            free(targetDirectory);
            return -1;
        }
        else // if it didn't fail, return 0
        {
            free(targetDirectory);
            return 0;
        }
    }
    else    // if arg[0] does not exist, we chdir to HOME
    {
        char * targetDirectory = getenv("HOME");
        chdir(targetDirectory);
        return 0;   // cd to HOME should be impossible to fail, no error handling
    }
}

int smallStatus()
{
    fprintf(stdout, "Exit status: %i\n", EXIT_STATUS);
    fflush(stdout);
    return 0;
}

int smallExec(struct input *userInput) // SORTA WORKING, FOREGROUND STUFF ISN'T WORKING
{
    spawnpid = -40;
    int childStatus = 0;
    int devNull = -1;
    spawnpid = fork();
    switch (spawnpid)
    {
        case -1: // fork returned -1, indicating it has failed in some manner
            fprintf(stderr, "Fork failed!"); 
            fflush(stderr);
            break;
        case 0:  // this is a child process, do some exec stuff
            if (userInput->backgroundProcess == 1)
            {
                processIsBackground = 1; // flag to tell handle_SIGINT how to treat this
                if (userInput->inRedirected != 1)
                {
                    devNull = open("/dev/null", O_RDONLY);
                    dup2(devNull, 0);
                }
                if (userInput->outRedirected != 1)
                {
                    devNull = open("/dev/null", O_WRONLY);
                    dup2(devNull, 1);
                }
            }
            else
            {
                SIGINT_action.sa_flags = 0;
                sigaction(SIGINT, &SIGINT_action, NULL);
                processIsBackground = 0; // flag to tell handle_SIGINT how to treat this
            }
            execvp(userInput->command, userInput->args);
            fprintf(stderr, "%s is not a valid command!\n", userInput->command); // if we're still running, execvp failed
            fflush(stderr);
            return 1;
        default: // this is the parent process
            if (userInput->backgroundProcess != 1) // the child is not a background process
            {
                childStatus = waitpid(spawnpid, &childStatus, 0); // wait for the child to terminate
                EXIT_STATUS = WEXITSTATUS(childStatus);
            }
            else // the child is a background process 
            {
                fprintf(stdout, "Background child PID: %i\n", spawnpid);	// print child pid
                fflush(stdout);
            }
            break;
    }
    return childStatus;
}

int inRedirect(struct input *userInput) // WORKING, I THINK
{
    int newFD = -1;

    newFD = open(userInput->inFileName, O_RDONLY);
    if (newFD == -1)
    {
        fprintf(stderr, "Error: File %s could not be opened.\n", userInput->inFileName);
        fflush(stderr);
        return 1;
    }
    else
    {
        dup2(newFD, 0); // 0 being stdin
        userInput->inRedirected = 1; // flag to tell unRedirect() to actually do something
        close(newFD);
        return 0;
    }
}

int outRedirect(struct input *userInput) // WORKING, I THINK
{
    int newFD = -1;

    newFD = open(userInput->outFileName, O_WRONLY | O_TRUNC | O_CREAT);
    if (newFD == -1)
    {
        fprintf(stderr, "Error: File %s could not be opened.\n", userInput->outFileName);
        fflush(stderr);
        return 1;
    }
    else
    {
        dup2(newFD, 1); // 1 being stdout
        userInput->outRedirected = 1; // flag to tell unRedirect() to actually do something
        close(newFD);
        return 0;
    }
}

void unRedirect(struct input *userInput)
{
    if (userInput->inRedirected == 1)
    {
        dup2(SAVE_STDIN, 0);
        userInput->inRedirected = 0;
    }
    if (userInput->outRedirected == 1)
    {
        dup2(SAVE_STDOUT, 1);
        userInput->outRedirected = 0;
    }
    return;
}

int main()
{
    SAVE_STDIN = dup(0);
    SAVE_STDOUT = dup(1);
    SIGINT_action.sa_handler = handle_SIGINT;
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    sigfillset(&SIGINT_action.sa_mask);
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGINT_action.sa_flags = SA_RESTART;
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGINT, &SIGINT_action, NULL);
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);
    struct input userInput;
    do {
        userInput.command = NULL;	// initialize all values of userInput to NULL or 0 as appropriate
        userInput.inFileName = NULL;
        userInput.inRedirected = 0;
        userInput.outFileName = NULL;
        userInput.outRedirected = 0;
        userInput.backgroundProcess = 0;
        for (int i = 0; i < MAX_ARGS; ++i)
        {
            userInput.args[i] = NULL;
        }
        parseInput(&userInput);
        if(userInput.command[0] == '#' || userInput.command[0] == '\n' || userInput.command[0] == '\0')
        {
            ;   // we ignore this blank or comment line and move on
        }
        else
        {
            if (userInput.inFileName != NULL)
            {
                EXIT_STATUS = inRedirect(&userInput);
            }
            if (userInput.outFileName != NULL)
            {
                EXIT_STATUS = outRedirect(&userInput);
            }
            if (strcmp(userInput.command, "exit") == 0)
            {
                kill(0, SIGTERM);   // send SIGKILL to entire process group. good thing we don't need to set an exit status
                return 0;   // this should never actually execute. but just in case
            }
            else if (strcmp(userInput.command, "cd") == 0)
            {
                smallCD(&userInput);
            }
            else if (strcmp(userInput.command, "status") == 0)
            {
                smallStatus(); // call smallStatus, probably do this after implementing background commands
            }
            else
            {
                smallExec(&userInput);
            }
            unRedirect(&userInput);
        }
        freeInputObject(&userInput);
    } while(strcmp(userInput.command, "exit") != 0);
    return 0;
}
