//
// Created by smigii on 2022-01-16.
//

#ifndef CRAVEN_UTILS_H
#define CRAVEN_UTILS_H

#define ADDR_LEN 64
#define PORT_LEN 6
#define NAME_LEN 16

char* trim(char *str)
{
	char *end;

	// Trim leading space
	while(isspace((unsigned char)*str)) str++;

	if(*str == 0)  // All spaces?
		return str;

	// Trim trailing space
	end = str + strlen(str) - 1;
	while(end > str && isspace((unsigned char)*end)) end--;

	// Write new null terminator character
	end[1] = '\0';

	return str;
}

void trim_r(char* str)
{
	char* end;

	// Trim trailing space
	end = str + strlen(str) - 1;
	while(end > str && isspace((unsigned char)*end)) end--;

	// Write new null terminator character
	end[1] = '\0';
}

#endif //CRAVEN_UTILS_H
