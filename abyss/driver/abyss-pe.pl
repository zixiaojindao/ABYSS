#use strict;
use Getopt::Long;

my $a = ""; 		#maximum number of branches of a bubble[2]
my $b = "";			#maximum length of a bubble (bp)[10000]
my $c = "";			#minimum mean k-mer coverage of a unitig [sqrt(median)]
my $d = "";			#allowable error of a distance estimate (bp)[6]
my $e = "";			#minimum erosion k-mer coverage [sqrt(median)] 
my $E = "";			#minimum erosion k-mer coverage per strand [1];
my $j = "";			#number of threads [2]
my $k = "";			#size of k-mer (bp) [25]
my $l = "";		    #minimum alignment length of a read (bp) [k]
my $m = "";			#minimum overlap of two unitigs (bp) [30]
my $n = "";			#minimum number of pairs required for building contigs [10]
my $N = "";			#minimum number of pairs required for building scaffolds [n]	
my $p = "";		    #minimum sequence identity of a bubble [0.9]
my $q = "";			#minimum base quality [3]
my $s = "";		    #minimum unitig size required for building contigs (bp) [200]
my $S = "";			#minimum contig size required for building scaffolds (bp) [s] 
my $t = "";			#minimum tip size (bp) [2k]
my $v = "";			#use v=-v to enable verbose logging [disabled]
my $aligner = ""    #align program
my $align = ""		#align program
my $fixmate = ""	#fixmate program
GetOptions(
	'a=s'		=> \$a,
	'b=s'		=> \$b,
	'c=s'		=> \$c,
	'd=s'		=> \$d,
	'e=s'		=> \$e,
	'E=s'		=> \$E,
	'j=s'		=> \$j,
	'k=s'		=> \$k,
	'l=s'		=> \$l,
	'm=s'		=> \$m,
	'n=s'		=> \$n,
	'N=s'		=> \$N,
	'p=s'		=> \$p,
	'q=s'		=> \$q,
	's=s'		=> \$s,
	'S=s'		=> \$S,
	't=s'		=> \$t,
	'v:s'		=> \$v,     
	'aligner=s' => \$aligner,
	'align=s'	=> \$align,
	'fixmate=s' => \$fixmate,
) or die "Incorrect usage!\n";

#@list=($a, $b, $c, $d, $e, $E, $j, $k, $l, $m, $n, $N, $p, $q, $s, $S, $t, $v);
#print("@list\n");
if($k eq "")
{
	die "k is need\n";
}

#ABYSS parameters
my $abyssopt = "";
if($q eq "")
{
	$q = "3";
}
$abyssopt .= "-k$k -q$q ";
if($e ne "")
{
	$abyssopt .= "-e$e ";
}
if($E ne "")
{
	$abyssopt .= "-E$E ";
}
if($t ne "")
{
	$abyssopt .= "-t$t ";
} 
if($c ne "")
{
	$abyssopt .= "-c$c ";
}
if($b ne "")
{
	$abyssopt .= "-b$b ";
}

#ADjList parameters
if($m eq "")
{
	$m = "50";
}

#PopBubbles parameters
my $pbopt = "";
if($b ne "")
{
	$pbopt .= "-b$b ";
}
if($p eq "")
{
	$p = "0.9";
}
$pbopt .= "-p$p ";

#Aligner parameters
my $mapopt = "";
if($aligner eq "")
{
	$aligner = "map";
}
if($align eq "")
{
	$align = "abyss-$aligner";
}
$mapopt = "$v -j$j -l$($*_l) $ALIGNER_OPTIONS $MAP_OPTIONS;


