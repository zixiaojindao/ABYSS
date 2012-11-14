#include"utility.h"
#include<iostream>
#include<cassert>
using namespace std;

void NotImplmentException(char* func)
{
	cout<<func<<" not implment"<<endl;
	assert(0);
}
