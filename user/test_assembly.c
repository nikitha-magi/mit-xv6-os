#include<stdio.h>

int main(void)
{
    unsigned int i = 0x00646c72;
	printf("H%x Wo%s", 57616, (char *) &i);
    // printf("%lu", sizeof(unsigned int));
    // getchar();
    return 0;
}