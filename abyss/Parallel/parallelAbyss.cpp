#include "config.h"
#include "Log.h"
#include "NetworkSequenceCollection.h"
#include "Assembly/Options.h"
#include "Common/Options.h"
#include "Timer.h"
#include "Uncompress.h"
#include <cerrno>
#include <climits> // for HOST_NAME_MAX
#include <cstdio> // for setvbuf
#include <cstdlib>
#include <cstring> // for strerror
#include <iostream>
#include <mpi.h>
#include <sstream>
//#include <unistd.h> // for gethostname
#include <localunistd.h>
#include <vector>
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 256
#endif

using namespace std;

/** Execute a shell command and check its return status. */
static void systemx(const string& command)
{
	if (opt::verbose > 0)
		cout << command << endl;
	int ret = system(command.c_str());
	if (ret == 0)
		return;
	cerr << "error: command failed: `" << command << "'\n";
	if (ret == -1)
		cerr << "system() failed: " << strerror(errno) << endl;
	exit(ret == -1 ? EXIT_FAILURE : ret);
}

/** Concatenate files using the specified command and remove them. */
static void concatenateFiles(const string& dest,
		const string& prefix, const string& suffix,
		const string& command = "cat")
{
	cout << "Concatenating to " << dest << endl;
	ostringstream s;
	s << command;
	//replace >'dest' with >dest  by Sun Zhao(zixiaojindao@gmail.com)
	for (int i = 0; i < opt::numProc; i++)
		s << ' ' << prefix << i << suffix;
	s << " >" << dest;
	systemx(s.str());

	bool die = false;
	for (int i = 0; i < opt::numProc; i++) {
		s.str("");
		s << prefix << i << suffix;
		//replace path type with string by Sun Zhao(zixiaojindao@gmail.com)
		//const char* path = s.str().c_str();
		string path = s.str();
		if (unlink(path.c_str()) == -1) {
			cerr << "error: removing `" << path << "': "
				<< strerror(errno) << endl;
			die = true;
		}
	}
	if (die)
		exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
	Timer timer("Total");

	// Set stdout to be line buffered.
	//replace 0 with 1024 by Sun Zhao(zixiaojindao@gmail.com)
	setvbuf(stdout, NULL, _IOLBF, 1024);

	MPI_Init(&argc,&argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &opt::rank);
	MPI_Comm_size(MPI_COMM_WORLD, &opt::numProc);

	opt::parse(argc, argv);
	if (opt::rank == 0)
		cout << "Running on " << opt::numProc << " processors\n";

	MPI_Barrier(MPI_COMM_WORLD);
	char hostname[HOST_NAME_MAX];
	gethostnamelocal(hostname, sizeof hostname);
	logger(0) << "Running on host " << hostname << endl;
	MPI_Barrier(MPI_COMM_WORLD);

	if (opt::rank == 0) {
		NetworkSequenceCollection networkSeqs;
		networkSeqs.runControl();
	} else {
		NetworkSequenceCollection networkSeqs;
		networkSeqs.run();
	}

	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Finalize();

	//modify awk cmd to adapt win awk by Sun Zhao(zixiaojindao@gmail.com)
	if (opt::rank == 0) {
		concatenateFiles(opt::contigsPath, "contigs-", ".fa",
				"awk \"/^>/ { $1=\\\"\">\\\"\" i++ } { print }\"");
		if (!opt::snpPath.empty())
			concatenateFiles(opt::snpPath, "snp-", ".fa");
		cout << "Done." << endl;
	}

	return 0;
}
