#!/usr/bin/perl

use strict;
use warnings;

my $i = 1;
my $t = 5000;

while ($i <= 1000) { 
	print "tst-random $i $t\n";

	system("./tst-random $i $t 2>/dev/null");

	die if ($? != 0);

	$i += 1;
}