#use strict;
use Getopt::Long;

my $a = 2;			#maximum number of branches of a bubble[2]
my $b = 10000;		#maximum length of a bubble (bp)[10000]
my $c;				#minimum mean k-mer coverage of a unitig [sqrt(median)]
my $d = 6;			#allowable error of a distance estimate (bp)[6]
my $e;				#minimum erosion k-mer coverage [sqrt(median)] 
my $E = 1;			#minimum erosion k-mer coverage per strand [1];
my $j = 2;			#number of threads [2]
my $k = 25;			#size of k-mer (bp) [25]
my $l = -1;		    #minimum alignment length of a read (bp) [k]
my $m = 30;			#minimum overlap of two unitigs (bp) [30]
my $n = 10;			#minimum number of pairs required for building contigs [10]
my $N = -1;			#minimum number of pairs required for building scaffolds [n]	
my $p = 0.9;		#minimum sequence identity of a bubble [0.9]
my $q = 3;			#minimum base quality [3]
my $s = 200;		#minimum unitig size required for building contigs (bp) [200]
my $S = -1;			#minimum contig size required for building scaffolds (bp) [s] 
my $t = -1;			#minimum tip size (bp) [2k]
my $v;				#use v=-v to enable verbose logging [disabled]

GetOptions(
        'a=i'		=> \$a,
		'b=i'		=> \$b,
		'c=f'		=> \$c,
		'd=i'		=> \$d,
		'e=i'		=> \$e,
		'E=i'		=> \$E,
		'j=i'		=> \$j,
		'k=i'		=> \$k,
		'l=i'		=> \$l,
		'm=i'		=> \$m,
		'n=i'		=> \$n,
		'N=i'		=> \$N,
		'p=f'		=> \$p,
		'q=f'		=> \$q,
		's=i'		=> \$s,
		'S=i'		=> \$S,
		't=i'		=> \$t,
		'v:i'		=> \$v,     
	) or die "Incorrect usage!\n";
#set dependent default
$l = ($l != -1)?$l:$k;
$N = ($N != -1)?$N:$n;
$t = ($t != -1)?$t:($k * 2);
$S = ($S != -1)?$S:$s;
@list=($a, $b, $c, $d, $e, $E, $j, $k, $l, $m, $n, $N, $p, $q, $s, $S, $t, $v);
print("@list\n");

