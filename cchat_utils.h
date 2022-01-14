//
// Created by smigii on 2022-01-14.
//

#ifndef NETWORKING_DRAGON_UTILS_H
#define NETWORKING_DRAGON_UTILS_H

void check_status(long status, long ok, const char* msg)
{
	if(status != ok) {
		printf("ofuck: [%s]\n", msg);
		exit(-1);
	}
}

#endif //NETWORKING_DRAGON_UTILS_H
