#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>            //termios, TCSANOW, ECHO, ICANON
#include <string.h>
#include <stdbool.h>
#include <errno.h>

const char * sysname = "seashell";

enum return_codes {
	SUCCESS = 0,
	EXIT = 1,
	UNKNOWN = 2,
};
struct command_t {
	char *name;
	bool background;
	bool auto_complete;
	int arg_count;
	char **args;
	char *redirects[3]; // in/out redirection
	struct command_t *next; // for piping
};
/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t * command)
{
	int i=0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background?"yes":"no");
	printf("\tNeeds Auto-complete: %s\n", command->auto_complete?"yes":"no");
	printf("\tRedirects:\n");
	for (i=0;i<3;i++)
		printf("\t\t%d: %s\n", i, command->redirects[i]?command->redirects[i]:"N/A");
	printf("\tArguments (%d):\n", command->arg_count);
	for (i=0;i<command->arg_count;++i)
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	if (command->next)
	{
		printf("\tPiped to:\n");
		print_command(command->next);
	}


}
/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command)
{
	if (command->arg_count)
	{
		for (int i=0; i<command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}
	for (int i=0;i<3;++i)
		if (command->redirects[i])
			free(command->redirects[i]);
	if (command->next)
	{
		free_command(command->next);
		command->next=NULL;
	}
	free(command->name);
	free(command);
	return 0;
}
/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt()
{
	char cwd[1024], hostname[1024];
    gethostname(hostname, sizeof(hostname));
	getcwd(cwd, sizeof(cwd));
	printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
	return 0;
}
/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command)
{
	const char *splitters=" \t"; // split at whitespace
	int index, len;
	len=strlen(buf);
	while (len>0 && strchr(splitters, buf[0])!=NULL) // trim left whitespace
	{
		buf++;
		len--;
	}
	while (len>0 && strchr(splitters, buf[len-1])!=NULL)
		buf[--len]=0; // trim right whitespace

	if (len>0 && buf[len-1]=='?') // auto-complete
		command->auto_complete=true;
	if (len>0 && buf[len-1]=='&') // background
		command->background=true;

	char *pch = strtok(buf, splitters);
	command->name=(char *)malloc(strlen(pch)+1);
	if (pch==NULL)
		command->name[0]=0;
	else
		strcpy(command->name, pch);

	command->args=(char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index=0;
	char temp_buf[1024], *arg;
	while (1)
	{
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch) break;
		arg=temp_buf;
		strcpy(arg, pch);
		len=strlen(arg);

		if (len==0) continue; // empty arg, go for next
		while (len>0 && strchr(splitters, arg[0])!=NULL) // trim left whitespace
		{
			arg++;
			len--;
		}
		while (len>0 && strchr(splitters, arg[len-1])!=NULL) arg[--len]=0; // trim right whitespace
		if (len==0) continue; // empty arg, go for next

		// piping to another command
		if (strcmp(arg, "|")==0)
		{
			struct command_t *c=malloc(sizeof(struct command_t));
			int l=strlen(pch);
			pch[l]=splitters[0]; // restore strtok termination
			index=1;
			while (pch[index]==' ' || pch[index]=='\t') index++; // skip whitespaces

			parse_command(pch+index, c);
			pch[l]=0; // put back strtok termination
			command->next=c;
			continue;
		}

		// background process
		if (strcmp(arg, "&")==0)
			continue; // handled before

		// handle input redirection
		redirect_index=-1;
		if (arg[0]=='<')
			redirect_index=0;
		if (arg[0]=='>')
		{
			if (len>1 && arg[1]=='>')
			{
				redirect_index=2;
				arg++;
				len--;
			}
			else redirect_index=1;
		}
		if (redirect_index != -1)
		{
			command->redirects[redirect_index]=malloc(len);
			strcpy(command->redirects[redirect_index], arg+1);
			continue;
		}

		// normal arguments
		if (len>2 && ((arg[0]=='"' && arg[len-1]=='"')
			|| (arg[0]=='\'' && arg[len-1]=='\''))) // quote wrapped arg
		{
			arg[--len]=0;
			arg++;
		}
		command->args=(char **)realloc(command->args, sizeof(char *)*(arg_index+1));
		command->args[arg_index]=(char *)malloc(len+1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count=arg_index;
	return 0;
}
void prompt_backspace()
{
	putchar(8); // go back 1
	putchar(' '); // write empty over
	putchar(8); // go back 1 again
}
/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command)
{
	int index=0;
	char c;
	char buf[4096];
	static char oldbuf[4096];

    // tcgetattr gets the parameters of the current terminal
    // STDIN_FILENO will tell tcgetattr that it should write the settings
    // of stdin to oldt
    static struct termios backup_termios, new_termios;
    tcgetattr(STDIN_FILENO, &backup_termios);
    new_termios = backup_termios;
    // ICANON normally takes care that one line at a time will be processed
    // that means it will return if it sees a "\n" or an EOF or an EOL
    new_termios.c_lflag &= ~(ICANON | ECHO); // Also disable automatic echo. We manually echo each char.
    // Those new settings will be set to STDIN
    // TCSANOW tells tcsetattr to change attributes immediately.
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);


    //FIXME: backspace is applied before printing chars
	show_prompt();
	int multicode_state=0;
	buf[0]=0;
  	while (1)
  	{
		c=getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		if (c==9) // handle tab
		{
			buf[index++]='?'; // autocomplete
			break;
		}

		if (c==127) // handle backspace
		{
			if (index>0)
			{
				prompt_backspace();
				index--;
			}
			continue;
		}
		if (c==27 && multicode_state==0) // handle multi-code keys
		{
			multicode_state=1;
			continue;
		}
		if (c==91 && multicode_state==1)
		{
			multicode_state=2;
			continue;
		}
		if (c==65 && multicode_state==2) // up arrow
		{
			int i;
			while (index>0)
			{
				prompt_backspace();
				index--;
			}
			for (i=0;oldbuf[i];++i)
			{
				putchar(oldbuf[i]);
				buf[i]=oldbuf[i];
			}
			index=i;
			continue;
		}
		else
			multicode_state=0;

		putchar(c); // echo the character
		buf[index++]=c;
		if (index>=sizeof(buf)-1) break;
		if (c=='\n') // enter key
			break;
		if (c==4) // Ctrl+D
			return EXIT;
  	}
  	if (index>0 && buf[index-1]=='\n') // trim newline from the end
  		index--;
  	buf[index++]=0; // null terminate string

  	strcpy(oldbuf, buf);

  	parse_command(buf, command);

  	// print_command(command); // DEBUG: uncomment for debugging

    // restore the old settings
    tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  	return SUCCESS;
}
int process_command(struct command_t *command);

char cd[1000];//current file path
FILE *fptr1 = NULL;
FILE *fptr2 = NULL;
FILE *fptr = NULL;
FILE *fptr0 = NULL;
int main()
{
	//save the current directory of the file 
		getcwd(cd, sizeof(cd));
		
	//
	while (1)
	{
		struct command_t *command=malloc(sizeof(struct command_t));
		memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

		int code;
		code = prompt(command);
		if (code==EXIT) break;

		code = process_command(command);
		if (code==EXIT) break;

		free_command(command);
	}

	printf("\n");
	return 0;
}

int process_command(struct command_t *command)
{
	int r;
	if (strcmp(command->name, "")==0) return SUCCESS;

	if (strcmp(command->name, "exit")==0)
		return EXIT;

	if (strcmp(command->name, "cd")==0)
	{
		if (command->arg_count > 0)
		{
			r=chdir(command->args[0]);
			if (r==-1)
				printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
			return SUCCESS;
		}
	}
	
	//baca command. Question 6 implementation
	if (strcmp(command->name, "baca")==0)
	//BaCa stands for Batu, Can. 
	//requires python3 being available in the terminal
	{
		if (command->arg_count == 0){
			pid_t pid=fork();
			if (pid==0){//execvp called in fork() to prevent parent from terminating the seashell
				//resolve python script path 
				char * bacaPath = malloc(sizeof(char) * 50);
				strcpy(bacaPath, cd);
				strcpy(bacaPath, "chimneyAnimation.py");
				char * arr[] = {"python3",bacaPath, NULL};
				execvp("python3", arr);
				free(bacaPath);
			}else{//parent
				wait(0);
				return SUCCESS;
			} 
		}
		
	}

	//kdiff command. implementation of Question5
	if (strcmp(command->name, "kdiff")==0)
	{
		char * flagParam;
		char * file1Param;
		char * file2Param;
		if (command->arg_count == 3){ //{-a,-b} file1 file2
			//parse the input params
			flagParam = malloc(sizeof(command->args[0]));
			file1Param = malloc(sizeof(command->args[1]));
			file2Param = malloc(sizeof(command->args[2]));
			
			flagParam = command->args[0]; //-b -a
			file1Param = command->args[1]; //file1
			file2Param = command->args[2]; //file2
			
		}else if(command->arg_count == 2){
			//parse the input params
			flagParam = malloc(sizeof(command->args[0]));
			file1Param = malloc(sizeof(command->args[1]));
			file2Param = malloc(sizeof(command->args[2]));
				
			flagParam = "-a"; //-b -a
			file1Param = command->args[0]; //file1
			file2Param = command->args[1]; //file2
		}else{
			printf("Invalid number of parameters supplied. Try again.");
			return SUCCESS;
		}
			//check for the flagParam
			if(strcmp(flagParam, "-a") == 0){
				//check if file extensions are the same
				char * file1Extension = malloc(sizeof(file1Param));
				char * file2Extension = malloc(sizeof(file1Param));
				bool file1Flag = 0;//0 for before . & 1 for after .
				bool file2Flag = 0;//0 for before . & 1 for after .
				int file1Count = 0;
				int file2Count = 0;
				
				//get file extension for file1
				for(int i = 0; i < strlen(file1Param); i++){
					if(file1Flag == 1){
						file1Extension[file1Count] = file1Param[i];
						file1Count++;
					}
					if(file1Param[i] == '.'){
						file1Flag = 1;
					}
				}
				file1Count = 0;
				file1Flag = 0;//0 for before . & 1 for after .
				
				

				//get file extension for file1
				for(int i = 0; i < strlen(file2Param); i++){
					if(file2Flag == 1){
						file2Extension[file2Count] = file2Param[i];
						file2Count++;
					}
					if(file2Param[i] == '.'){
						file2Flag = 1;
					}
				}

				file2Count = 0;
				file2Flag = 0;//0 for before . & 1 for after .

				//check if extensions are the same
				if(strcmp(file1Extension, file2Extension) != 0){//if extensions don't match
					printf("Files are not text files.\n");
					return SUCCESS;
				}

				//open file1
				//get current directory
				char cwd[1000];
				getcwd(cwd, sizeof(cwd));
				//create path name to the text file
				char * file1Path = malloc(sizeof(cwd) + sizeof(file1Param));
				strcpy(file1Path, cwd);
				strcat(file1Path, "/");
				strcat(file1Path, file1Param);

				fptr1 = NULL;
				//file is open for both reading and appending & if file doesn't exists it is created
				if(fptr1 == NULL){
					char * file1Path = malloc(sizeof(cwd) + sizeof(file1Param));
					strcpy(file1Path, cwd);
					strcat(file1Path, "/");
					strcat(file1Path, file1Param);
					fptr1 = fopen(file1Path,"r"); 
				}else{
					fptr1 = freopen(file1Path,"r", fptr1); 
				}
				
				
				if(fptr1 == NULL){
					printf("Error!");   
				}

				//open file2
				//create path name to the text file
				char * file2Path = malloc(sizeof(cwd) + sizeof(file2Param));
				strcpy(file2Path, cwd);
				strcat(file2Path, "/");
				strcat(file2Path, file2Param);


				fptr2 = NULL; 
				//file is open for both reading and appending & if file doesn't exists it is created
				
				if(fptr2 == NULL){
					char * file2Path = malloc(sizeof(cwd) + sizeof(file2Param));
					strcpy(file2Path, cwd);
					strcat(file2Path, "/");
					strcat(file2Path, file2Param);
					fptr2 = fopen(file2Path,"r"); 
				}else{
					fptr2 = freopen(file2Path,"r", fptr2); 
				}

				
				
				if(fptr2 == NULL){
					printf("Error!");   
				}


				char * line1 = NULL;
				size_t len1 = 0;
				ssize_t read1;

				char * line2 = NULL;
				size_t len2 = 0;
				ssize_t read2;

				read1 = getline(&line1, &len1, fptr1);
				read2 = getline(&line2, &len2, fptr2); 
				int diffLineCount = 0; //# number different lines btw file1 & file2
				int lineNumber = 1; // keeps track of current line number
				while ((read1  != -1) && (read2 != -1)) {
					if(strcmp(line1, line2) != 0){
						printf("%s : Line: %d : %s\n",file1Param, lineNumber, line1);
						printf("%s : Line: %d : %s\n",file2Param, lineNumber, line2);
						diffLineCount++;	
					}
					lineNumber++;
					read1 = getline(&line1, &len1, fptr1);
					read2 = getline(&line2, &len2, fptr2);
				}

				if((read1  == -1) && (read2 == -1)){
					if(diffLineCount == 0){
						printf("The two files are identical\n");
					}else{
						printf("%d different lines found\n", diffLineCount);
					}

				}else{
					if(read1 != -1){ // read1 has remaining lines
						while(read1 != -1){//account for the remaining lines in file1
							diffLineCount++;
							read1 = getline(&line1, &len1, fptr1);
						}
						printf("%d different lines found\n", diffLineCount);
					}else{// read2 has remaining lines
						while(read2 != -1){//account for the remaining lines in file2
							diffLineCount++;
							read2 = getline(&line2, &len2, fptr2);
						}
						printf("%d different lines found\n", diffLineCount);
					}
				}

				rewind(fptr1);
				rewind(fptr2);

				fclose(fptr1);
				fclose(fptr2);

				free(file1Extension);
				free(file2Extension);
				free(file1Path);
				free(file2Path);
				

			}else if(strcmp(flagParam, "-b") == 0){
				//open file1
				FILE *fptr3;
				//file is open for both reading and appending & if file doesn't exists it is created
				
				fptr3 = fopen(file1Param,"a+"); 
				
				if(fptr3 == NULL){
					printf("Error!\n");   
				}

				//open file2
				FILE *fptr4;
				//file is open for both reading and appending & if file doesn't exists it is created
				
				fptr4 = fopen(file2Param, "a+"); 
				
				if(fptr4 == NULL){
					printf("Error!\n");   
				}

				//compare the binary files
				int c1, c2;
				int diffByteCount = 0;
				c1 = getc(fptr3);
				c2 = getc(fptr4);
				while (c1 != EOF && c2 != EOF) {
					if (c1 != c2 ){
						diffByteCount++;
					}
					c1 = getc(fptr3);
					c2 = getc(fptr4);
				}
				if (c1 == c2 && diffByteCount == 0) {// files are identical
					printf("The two files are identical\n");

				}else if(diffByteCount != 0){
					printf("The two files are different in %d bytes\n", diffByteCount);
				}else if(c1 != EOF){
					while(c1 != EOF){
						diffByteCount++;
						c1 = getc(fptr3);
					}
					printf("The two files are different in %d bytes\n", diffByteCount);

				}else if(c2 != EOF){
					while(c2 != EOF){
						diffByteCount++;
						c2 = getc(fptr4);
					}
					printf("The two files are different in %d bytes\n", diffByteCount);
				}
				fclose(fptr3);
				fclose(fptr4);
			}
						
			return SUCCESS;
		
	}

	//goodMorning command. implementation of Question4
	if (strcmp(command->name, "goodMorning")==0)
	{
		
		if (command->arg_count >= 2){

			//parse the arguments passed
			char * scheduleDate = malloc(sizeof(char) * strlen(command->args[0]));
			strcpy(scheduleDate, command->args[0]);
			char * scheduledMusic = malloc(sizeof(char) * strlen(command->args[1]));
			strcpy(scheduledMusic, command->args[1]);

			//parse scheduledDate into minute and hour (those are separated by ".") 
			char * scheduledHour = malloc(sizeof(char) * strlen(scheduleDate));
			char * scheduledMinute = malloc(sizeof(char) * strlen(scheduleDate));
			int hourCount = 0;
			int minuteCount = 0;

			bool minuteHourFlag = 0; // 0 is for hour & 1 is for minute
			for(int i = 0; i < strlen(scheduleDate); i++){
				//check for "."
				if(scheduleDate[i] == '.'){
					minuteHourFlag = 1;
					continue;
				}

				if(minuteHourFlag == 0){//for hour
					// strcpy(scheduledHour[hourCount], scheduleDate[i]);
					scheduledHour[hourCount] = scheduleDate[i];
					hourCount++;
				}else{//for hour
					// strcpy(scheduledMinute[minuteCount], scheduleDate[i]);
					scheduledMinute[minuteCount] = scheduleDate[i];
					minuteCount++;
				}
			}
			minuteHourFlag = 0;//reset the flag
			hourCount = 0;//reset counts
			minuteCount = 0;
			// printf("hour: %s \t minute: %s \n", scheduledHour, scheduledMinute); //uncomment for debugging

			//open file for writing the crontab info
			//create path name to the text file
			char * cronFilePath = malloc(sizeof(cd));
			strcat(cronFilePath, cd);
			strcat(cronFilePath,"/cronfile.txt");

			//clear the file
			remove(cronFilePath);
			
			FILE *fptr;
			//file is open for both reading and appending & if file doesn't exists it is created
			
			fptr = fopen(cronFilePath,"a+"); 
			
			if(fptr == NULL){
				printf("Error!");   
			}

			

			//write crontab info to the file
			fprintf(fptr,"%s %s * * * DISPLAY=:0.0 rhythmbox-client --play-uri %s\n", scheduledMinute, scheduledHour, scheduledMusic);
			fclose(fptr);

			//run crontab on crontfile.txt
			pid_t pid=fork();
			if (pid==0){//execvp called in fork() to prevent parent from terminating the seashell
				char * arr[] = {"crontab",cronFilePath,NULL};
				execvp("crontab", arr);
				free(scheduleDate);
				free(scheduledMusic);
				free(scheduledHour);
				free(scheduledMinute);
				free(cronFilePath);
			}else{//parent
				wait(0);
				return SUCCESS;
			} 
					
		}
	}

	//highlight command. implementation of Question3
	if (strcmp(command->name, "highlight")==0){

		if (command->arg_count > 0){
			//parse the input parameters
			char * language = malloc(sizeof(char) * (strlen(command->args[0]) + 1));
			char * color = malloc(sizeof(char) * (strlen(command->args[1]) + 1));
			char * filePath = malloc(sizeof(char) * (strlen(command->args[2]) + 1));
			strcpy(language, command->args[0]); 
			strcpy(color, command->args[1]);
			strcpy(filePath, command->args[2]); 

			//color codes
			char *DEFAULT = "\x1B[0m";
			char *RED = "\x1B[41m";
			char *GREEN = "\x1B[42m";
			char *BLUE = "\x1B[44m";

			//open the filePath txt file
			//read and write rel. information to/from chdirMem.txt file
			fptr0 = NULL;
			//file is open for both reading and appending & if file doesn't exists it is created
			
			fptr0 = fopen(filePath,"a+"); 
			
			if(fptr0 == NULL){
				printf("Error!");   
			}

			printf(" ");//for aesthetic purposes
			
			char * line = NULL;
			size_t len = 0;
			ssize_t read;
			
			//read txt file line by line
			char arr[2000][50];
			int index = 0;
			int priorIndex = 0;
			while ((read = getline(&line, &len, fptr0)) != -1) {
				char* token2 = strtok(line, " ");
				
				while (token2 != NULL) {
					strcpy(arr[index], token2);
					index++;
					token2 = strtok(NULL, " ");
				}		

				
				for(int i = priorIndex; i< index; i++){

					if(strcmp(language, arr[i]) == 0){//check if word is == language
						//check for color code
						if(strcmp(color, "r") == 0){
							printf(RED);
							printf("%s", language);
							printf(DEFAULT);
							printf(" ");
						}else if(strcmp(color, "g") == 0){
							printf(GREEN);
							printf("%s", language);
							printf(DEFAULT);
							printf(" ");
						}else if(strcmp(color, "b") == 0){
							printf(BLUE);
							printf("%s", language);
							printf(DEFAULT);
							printf(" ");
						}

					}else{ //if word != language
						printf("%s ", arr[i]);
					}
				}	

				priorIndex = index;
				
				
			}
			//free mem space
			fclose(fptr0);
			free(language);
			free(color);
			free(filePath);

			

			//highlight language r textFile.txt
			printf("%s","\n");
			return SUCCESS;
		}
	}

	//shortdir command. implementation of Question2
	if (strcmp(command->name, "shortdir")==0){
		//get current directory
		char cwd[1000];
		getcwd(cwd, sizeof(cwd));
		//create path name to the text file
		char * chdirMemPath = malloc(sizeof(cwd));
		strcpy(chdirMemPath, cd);
		strcat(chdirMemPath,"/chdirMem.txt");
		//read and write rel. information to/from chdirMem.txt file
		fptr = NULL;
		//file is open for both reading and appending & if file doesn't exists it is created
		
		fptr = fopen(chdirMemPath,"a+"); 
		
		if(fptr == NULL){
			printf("Error!");   
			
		}

		
		
		char* param = command->args[0];// passed param that indicates the operation {set, jump, del, clear, list}
		if (command->arg_count == 2){// can be {set, jump, del} ops.
		char* param2 = command->args[1]; // second passed param after the operation type

			if(strcmp(param, "set") == 0){
				char * writeParam = malloc(sizeof(char) * 1000);
				strcat(writeParam, param2);
				strcat(writeParam, "$");
				strcat(writeParam, cwd);
				fprintf(fptr,"%s\n", writeParam);
				
				
				
			}else if(strcmp(param, "jump") == 0){
				char * line = NULL;
				size_t len = 0;
				ssize_t read;
				//iterate through chdirMem.txt line by line
				while ((read = getline(&line, &len, fptr)) != -1) {
					// printf("%s", line); //uncomment for debug
					//parse file to bits
					char* token = strtok(line, "$:");
					
					int count = 0;// 0 for pathAlias & 1 for pathDir
					char * pathAlias = malloc(sizeof(char)*1000);
					char * pathDir = malloc(sizeof(char)*1000);
					while (token != NULL) {
						if(count == 0){
							strcpy(pathAlias, token);
						}else if(count == 1){
							strcpy(pathDir, token);
						}
						token = strtok(NULL, "$");
						count++;
					}
					
					

					if(strcmp(param2, pathAlias) == 0){// if the current alias mathces the passed param
						//chdir to pathDir
						pathDir[strlen(pathDir) - 1] = '\0';
			
						r=chdir(pathDir);
						if (r==-1){//if error occurs report it
							printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
						}

						
					}

					free(pathAlias);
					free(pathDir);
					
				}
			
			
			}else if(strcmp(param, "del") == 0){
				char * fullTextFile = NULL;
				fullTextFile = malloc(sizeof(char) * 100000);
				
				char * line = NULL;
				size_t len = 0;
				ssize_t read;
				//iterate through chdirMem.txt line by line
				while ((read = getline(&line, &len, fptr)) != -1) {
					// printf("%s", line); //uncomment for debug
					//parse file to bits
					char* token = strtok(line, "$");
					
					int count = 0;// 0 for pathAlias & 1 for pathDir
					char * pathAlias = NULL;
					char * pathDir = NULL;
					pathAlias = malloc(sizeof(char)*1000);
					pathDir = malloc(sizeof(char)*1000);
					while (token != NULL) {
						if(count == 0){
							strcpy(pathAlias, token);
						}else if(count == 1){
							strcpy(pathDir, token);
						}
						token = strtok(NULL, "$");
						count++;
					}
					
					

					if(strcmp(param2, pathAlias) != 0){// if the current alias doesn't match the passed param
						char * temp = malloc(sizeof(char)* 1000);
						strcpy(temp, pathAlias);
						strcat(temp,"$");
						strcat(temp, pathDir);
						strcat(fullTextFile, temp);//add the lines fullTextFile
					}
					free(pathAlias);
					free(pathDir);
				}
				//delete the file
				remove(chdirMemPath);
				fclose(fptr);
				//create the text file again
				fptr = NULL;
				//file is open for both reading and appending & if file doesn't exists it is created
				
				fptr = fopen(chdirMemPath,"a+"); 
				
				if(fptr == NULL){
					printf("Error!");   			
				}

				//write lines back to file
				fprintf(fptr,"%s\n", fullTextFile);
				
				free(fullTextFile);
				
				fclose(fptr);
				return SUCCESS;
			}
			fclose(fptr);
			return SUCCESS;
			}else if(command->arg_count == 1){//can be {clear, list} ops.
				if(strcmp(param, "clear") == 0){
					remove(chdirMemPath);
					return SUCCESS;
					
				}else if(strcmp(param, "list") == 0){
					char * line = NULL;
					size_t len = 0;
					ssize_t read;
					//iterate through chdirMem.txt line by line
					while ((read = getline(&line, &len, fptr)) != -1) {
						// printf("%s", line); //uncomment for debug
						//parse file to bits
						char* token = strtok(line, "$");
						
						int count = 0;// 0 for pathAlias & 1 for pathDir
						char * pathAlias = NULL;
						char * pathDir = NULL;
						pathAlias = malloc(sizeof(char)*1000);
						pathDir = malloc(sizeof(char)*1000);
						while (token != NULL) {
							if(count == 0){
								strcpy(pathAlias, token);
							}else if(count == 1){
								strcpy(pathDir, token);
							}
							token = strtok(NULL, "$");
							count++;
						}
						if(strlen(pathDir) > 0 && strlen(pathAlias)){
							printf("name: %s directory: %s\n", pathAlias, pathDir);	
						}
						
						free(pathAlias);
						free(pathDir);
				}
				fclose(fptr);
				return SUCCESS;
			}
		}


		fclose(fptr);
		free(chdirMemPath);
		
	}


	//using execv() and solving the path. implementation of Question1
	pid_t pid=fork();
	if (pid==0) // child
	{
		/// This shows how to do exec with environ (but is not available on MacOs)
	    // extern char** environ; // environment variables
		// execvpe(command->name, command->args, environ); // exec+args+path+environ

		/// This shows how to do exec with no auto-path resolve
		// add a NULL argument to the end of args, and the name to the beginning
		// as required by exec

		// increase args size by 2
		command->args=(char **)realloc(
			command->args, sizeof(char *)*(command->arg_count+=2));

		// shift everything forward by 1
		for (int i=command->arg_count-2;i>0;--i)
			command->args[i]=command->args[i-1];

		// set args[0] as a copy of name
		command->args[0]=strdup(command->name);
		// set args[arg_count-1] (last) to NULL
		command->args[command->arg_count-1]=NULL;

		//execvp(command->name, command->args); // exec+args+path
		/// TODO: do your own exec with path resolving using execv()
		//attain the $PATH variable
		const char* path = getenv("PATH");

		char* token = strtok(path, ":");

		while( token != NULL ) {
			//check for the validity
			char * argsPath = malloc(sizeof(char) * strlen(token));
			strcat(argsPath,token);
			strcat(argsPath, "/");
			strcat(argsPath,(command->name));
			int result = access(argsPath, F_OK);


			//printf("%s \n", token);

			if(result == 0){//if command exists in the path
				strcpy(command->args[0], argsPath);
				execv(argsPath, command->args);

			}else{//if command doesn't exist in the path
				token = strtok(NULL, ":");
			}

			
			
		}
		
		exit(0);
		
	}
	else
	{
		if (!command->background)
			wait(0); // wait for child process to finish
		return SUCCESS;
	}

	// TODO: your implementation here
	
	printf("-%s: %s: command not found\n", sysname, command->name);
	return UNKNOWN;
}
