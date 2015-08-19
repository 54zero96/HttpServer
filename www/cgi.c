#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> // pipe

int main(void){
	char *word;
	word = getenv("name");
	printf("How old are you?\r\n%s", word);
//	printf("The weather is nice, I like it.\r\n");
	word = getenv("wifi");
	printf("Your wifi: %s\t", word);
	printf("CHEERS\n");
}