#include"utility.h"
#include<iostream>
#include<cassert>
using namespace std;

void NotImplmentException(char* func)
{
	cerr<<func<<" not implment"<<endl;
	assert(0);
}
