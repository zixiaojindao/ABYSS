#include<Winsock2.h>
#include"localunistd.h"
#pragma comment(lib, "Ws2_32.lib")

int gethostnamelocal(char *name, int namelen)
{
	return gethostname(name, namelen);
}
