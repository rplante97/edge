#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>



int main(int argc, char *argv[])
{
	char * encoder_input;
	int fd;
	int input_len;

	encoder_input = argv[1];
	input_len = strlen(encoder_input);
	
	//Open the edge device file
	fd = open("/dev/edge", O_WRONLY);
	if(fd < 0){
		printf("Error opening device file!");
		return -1;
	}
	
	if(write(fd, encoder_input, input_len) < 0){
		printf("Failed to write to device!");
		return -1;	
	}
	close(fd);
	return 0;
}
