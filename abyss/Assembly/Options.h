#ifndef ASSEMBLY_OPTIONS_H
#define ASSEMBLY_OPTIONS_H 1

#include <string>
#include <vector>

namespace opt {
	extern int kmerSize;
	extern int kMin;
	extern int kMax;
	extern int kStep;
	extern unsigned erode;
	extern unsigned erodeStrand;
	extern int trimLen;
	extern float coverage;
	extern int bubbleLen;
	extern std::string coverageHistPath;
	extern std::string contigsPath;
	extern std::string contigsTempPath;
	extern std::string graphPath;
	extern std::string snpPath;
	extern std::vector<std::string> inFiles;

	void parse(int argc, char* const* argv);
}

#endif
