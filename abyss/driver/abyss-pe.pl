#use strict;
use Getopt::Long;

my $name = ""
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
my $np = ""
GetOptions(
	'name=s'	=> \$name
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
	'np=s'		=> \$np,
) or die "Incorrect usage!\n";

#@list=($a, $b, $c, $d, $e, $E, $j, $k, $l, $m, $n, $N, $p, $q, $s, $S, $t, $v);
#print("@list\n");

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
$mapopt = "$v -j$j -l$($*_l) $ALIGNER_OPTIONS $MAP_OPTIONS";

#fixmate parameters
my $fmopt = "";
if($align eq "abyss-kaligner")
{
	if($fixmate eq "")
	{
		$fixmate = "ParseAligns";
	}
	$fmopt = "$v $FIXMATE_OPTIONS";
}
else
{
	if($fixmate eq "")
	{
		$fixmate = "abyss-fixmate";
	}
	$fmopt = "$v $FIXMATE_OPTIONS";
}

#DistanceEst parameters
my $libs, $deopt;
if($l eq "")
{
	$l = $k;
}
if($s eq "")
{
	$s = "200";
}
if($n eq "")
{
	$n = "10";
}
#$libs=$(pe) $(mp)
#$(foreach i,$(libs),$(eval $i_l?=$l))
#$(foreach i,$(libs),$(eval $i_s?=$s))
#$(foreach i,$(libs),$(eval $i_n?=$n))
#deopt=$v -j$j -k$k -l$($*_l) -s$($*_s) -n$($*_n) $($*_de) \
#	$(DISTANCEEST_OPTIONS)

#SimpleGraph parameters
my $sgopt = "";
if($d eq "")
{
	$sgopt .= "-d$d";
}

#PathConsensus parameters
my $pcopt = "";
if($a eq "")
{
	$pcopt .= "-a$a";
}
$pcopt .= "-p$p";

#Scaffold parameters
if($S eq "")
{
	$S = $s;
}
if($N eq "")
{
	$N = $n;
}

#Remove environment variables
#unexport in se $(lib) $(pe) $(mp)

#Check the mandatory parameters
if($name eq "")
{
	die("abyss-pe: missing parameter \'name\'");
}
if($k eq "")
{
	die("abyss-pe: missing parameter \'k\'");
}
#ifeq ($(lib)$(in)$(se),)
#error::
#	@>&2 echo 'abyss-pe: missing parameter `lib`, `in` or `se`'
#endi

# Determine the default target
#default: unitigs
#ifneq ($(in),)
#default: contigs contigs-dot
#endif
#ifneq ($(mp),)
#default: scaffolds scaffolds-dot
#endif
#default: stat

#Define the commands (phony targets)
my $unitigs = "$name-unitigs.fa";
my $unitigs_dot = "$name-unitigs.dot";
my $pe_index = "$name-3.fa.fm";
#my $pe_sam = "$(addffix -3.sam.gz, $(pe)); 
#my $pe_bam = "$addsuffix -3.bam.bai, $(pe));
my $contigs = "$name-contigs.dot";
my $contigs_dot = "$name-contigs.dot";
my $mp_index = "$name-6.fa.fm";
#my $mp_sam = "(addsuffix -6.sam.gz, $(mp));
#my $mp_bam = "(addsuffix -6.bam.bai, $(mp));
my $scaffolds = "$name-scaffolds.fa";
my $scaffolds_dot = "$name-scaffolds.dot";

#all: default bam stats
#clean:
#	rm -f *.adj *.dot *.sam.gz *.hist *.dist *.path *.path[123]
#.PHONY: bam default stats \
#	unitigs unitigs-dot \
#	pe-index pe-sam pe-bam contigs contigs-dot \
#	mp-index mp-sam mp-bam scaffolds scaffolds-dot \
#	all clean help version versions
#.DELETE_ON_ERROR:
#.SECONDARY

#Utilities
#%.fa.fm: %.fa
#	abyss-index $v $<
#
#%.bam: %.sam.gz
#	samtools view -Sb $< -o $@
#
#%.bam.bai: %.bam
#	samtools index $

#Assemble unitigs
if(np ne "")
{
	#$(mpirun) -np $(np) ABYSS-P $(abyssopt) $(ABYSS_OPTIONS) -o $@ $(in) $(se)
}
else
{
	#ABYSS $(abyssopt) $(ABYSS_OPTIONS) -o $@ $(in) $(se)
}

#Find overlapping contigs

%-1.adj: %-1.fa
	AdjList $v -k$k -m$m $< >$@

# Remove shim contigs

%-2.adj: %-1.adj
	abyss-filtergraph $v -k$k -g $@ $^ >$*-1.path

# Pop bubbles

%-2.path %-3.adj: %-1.fa %-2.adj
	PopBubbles $v -j$j -k$k $(pbopt) $(POPBUBBLES_OPTIONS) -g $*-3.adj $^ >$*-2.path

%-3.fa: %-1.fa %-2.adj %-2.path
	MergeContigs $v -k$k -o $@ $^
	awk '!/^>/ {x[">" $$1]=1; next} {getline s} $$1 in x {print $$0 "\n" s}' \
		$*-2.path $*-1.fa >$*-indel.fa

%-3.dot: %-3.adj
	abyss-todot $v -k$k $< >$@

%-unitigs.fa: %-3.fa
	ln -sf $< $@

%-unitigs.dot: %-3.dot
	ln -sf $< $@

# Estimate distances between unitigs

%-3.sam.gz %-3.hist: $(name)-3.fa
	$(align) $(mapopt) $(strip $($*)) $< \
		|$(fixmate) $(fmopt) -h $*-3.hist \
		|sort -snk3 -k4 \
		|gzip >$*-3.sam.gz

%-3.bam %-3.hist: $(name)-3.fa
	$(align) $(mapopt) $(strip $($*)) $< \
		|$(fixmate) $(fmopt) -h $*-3.hist \
		|sort -snk3 -k4 \
		|samtools view -Sb - -o $*-3.bam

%-3.dist: %-3.sam.gz %-3.hist
	gunzip -c $< \
	|DistanceEst $(deopt) -o $@ $*-3.hist

%-3.dist: %-3.bam %-3.hist
	samtools view -h $< \
	|DistanceEst $(deopt) -o $@ $*-3.hist

%-3.dist: $(name)-3.fa
	$(align) $(mapopt) $(strip $($*)) $< \
		|$(fixmate) $(fmopt) -h $*-3.hist \
		|sort -snk3 -k4 \
		|DistanceEst $(deopt) -o $@ $*-3.hist

dist=$(addsuffix -3.dist, $(pe))

ifneq ($(name)-3.dist, $(dist))
$(name)-3.dist: $(name)-3.fa $(dist)
	abyss-todot $v --dist -e $^ >$@

$(name)-3.bam: $(addsuffix -3.bam, $(pe))
	samtools merge -r $@ $^
endif

# Find overlaps between contigs

%-4.fa %-4.adj: %-3.fa %-3.adj %-3.dist
	Overlap $v $(OVERLAP_OPTIONS) -k$k -g $*-4.adj -o $*-4.fa $^

# Assemble contigs

%-4.path1: %-4.adj %-3.dist
	SimpleGraph $v $(sgopt) $(SIMPLEGRAPH_OPTIONS) -j$j -k$k -o $@ $^

%-4.path2: %-4.adj %-4.path1
	MergePaths $v $(MERGEPATHS_OPTIONS) -j$j -k$k -o $@ $^

%-4.path3: %-4.adj %-4.path2
	PathOverlap --assemble $v -k$k $^ >$@

ifndef cs

%-5.path %-5.fa %-5.adj: %-3.fa %-4.fa %-4.adj %-4.path3
	cat $(wordlist 1, 2, $^) \
		|PathConsensus $v -k$k $(pcopt) -o $*-5.path -s $*-5.fa -g $*-5.adj - $(wordlist 3, 4, $^)

%-6.fa: %-3.fa %-4.fa %-5.fa %-5.adj %-5.path
	cat $(wordlist 1, 3, $^) |MergeContigs $v -k$k -o $@ - $(wordlist 4, 5, $^)

else

%-5.adj %-5.path: %-4.adj %-4.path3
	ln -sf $*-4.adj $*-5.adj
	ln -sf $*-4.path3 $*-5.path

%-cs.fa: %-3.fa %-4.fa %-4.adj %-4.path3
	cat $(wordlist 1, 2, $^) |MergeContigs $v -k$k -o $@ - $(wordlist 3, 4, $^)

# Convert colour-space sequence to nucleotides

%-6.fa: %-cs.fa
	KAligner $v --seq -m -j$j -l$l $(in) $(se) $< \
		|Consensus $v -o $@ $<

endif

%-6.dot: %-5.adj %-5.path
	PathOverlap --overlap $v --dot -k$k $^ >$@

%-contigs.fa: %-6.fa
	ln -sf $< $@

%-contigs.dot: %-6.dot
	ln -sf $< $@

# Estimate distances between contigs

%-6.sam.gz %-6.hist: $(name)-6.fa
	$(align) $(mapopt) $(strip $($*)) $< \
		|$(fixmate) $(fmopt) -h $*-6.hist \
		|sort -snk3 -k4 \
		|gzip >$*-6.sam.gz

%-6.bam %-6.hist: $(name)-6.fa
	$(align) $(mapopt) $(strip $($*)) $< \
		|$(fixmate) $(fmopt) -h $*-6.hist \
		|sort -snk3 -k4 \
		|samtools view -Sb - -o $*-6.bam

%-6.dist.dot: %-6.sam.gz %-6.hist
	gunzip -c $< \
	|DistanceEst --dot $(deopt) -o $@ $*-6.hist

%-6.dist.dot: %-6.bam %-6.hist
	samtools view -h $< \
	|DistanceEst --dot $(deopt) -o $@ $*-6.hist

%-6.dist.dot: $(name)-6.fa
	$(align) $(mapopt) $(strip $($*)) $< \
		|$(fixmate) $(fmopt) -h $*-6.hist \
		|sort -snk3 -k4 \
		|DistanceEst --dot $(deopt) -o $@ $*-6.hist

# Scaffold

%-6.path1: $(name)-6.dot $(addsuffix -6.dist.dot, $(mp))
	abyss-scaffold $v -k$k -s$S -n$N -g $@.dot $(SCAFFOLD_OPTIONS) $^ >$@

%-6.path2: %-6.fa %-6.dot %-6.path1
	PathConsensus $v -k$k -p1 -s /dev/null -o $@ $^

%-7.fa %-7.dot: %-6.fa %-6.dot %-6.path2
	MergeContigs $v -k$k -o $*-7.fa -g $*-7.dot $^

%-scaffolds.fa: %-7.fa
	ln -sf $< $@

%-scaffolds.dot: %-7.dot
	ln -sf $< $@

# Create the final BAM file

ifneq ($(mp),)
bam: $(name)-scaffolds.bam.bai
else
ifneq ($(in),)
bam: $(name)-contigs.bam.bai
else
bam: $(name)-unitigs.bam.bai
endif
endif

$(name)-unitigs.bam: %.bam: %.fa
	$(align) $v -j$j -l$l $(ALIGNER_OPTIONS) $(se) $< \
		|samtools view -Su - |samtools sort -o - - >$@

$(name)-contigs.bam $(name)-scaffolds.bam: %.bam: %.fa
	$(align) $v -j$j -l$l $(ALIGNER_OPTIONS) \
		$(call map, deref, $(sort $(lib) $(pe) $(mp))) $< \
		|$(fixmate) $(fmopt) \
		|sort -snk3 -k4 \
		|samtools view -Sb - >$@

# Align the variants to the assembly

%.fa.bwt: %.fa
	bwa index $<

%-variants.bam: %.fa.bwt
	bwa bwasw -t$j $*.fa <(cat $(name)-bubbles.fa $(name)-indel.fa) \
		|samtools view -Su - |samtools sort -o - - >$@

%-variants.vcf.gz: %.fa %-variants.bam
	samtools mpileup -Buf $^ |bcftools view -vp1 - |bgzip >$@

%.gz.tbi: %.gz
	tabix -pvcf $<

# Calculate assembly contiguity statistics

stats: $(name)-stats

$(name)-stats: %-stats: %-unitigs.fa
ifneq ($(in),)
$(name)-stats: %-stats: %-contigs.fa
endif
ifneq ($(mp),)
$(name)-stats: %-stats: %-scaffolds.fa
endif
$(name)-stats:
	abyss-fac $(FAC_OPTIONS) $^ |tee $@

# Create an AGP file and FASTA file of scaftigs from scaffolds

%.agp %-agp.fa: %.fa
	abyss-fatoagp $(FATOAGP_OPTIONS) -f $*-agp.fa $< >$*.agp

# Align the contigs to the reference

%-$(ref).sam.gz: %.fa
	bwa bwasw -t$j $(BWASW_OPTIONS) $($(ref)) $< |gzip >$@

# Find breakpoints in the alignments

%.break: %.sam.gz
	sam2break $(SAM2BREAK_OPTIONS) $< >$@
