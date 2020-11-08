
$fn = "lib80.pex";
open $in, "<$fn" or die "can't open $fn\n";

while (<$in>) {
	if (/^\$file\((.*)\)/) {
		$file = $1;
	} elsif (/^#(\w\S+)/) {
		push @{$ext{$1}}, $file;
	} elsif (/^(\w\S+)\s+(.)/) {
		$ref{$1}->{TYPE} = $2;
		push @{$ref{$1}->{FILES}}, $file;
	}
}

print "Undefined\n";
for $i (sort keys %ext) {
	if (!defined($ref{$i})) {
		print join(",", @{$ext{$i}}), "\t$i\n";
	}
}
print "\nPublics\n";
for $i (sort keys %ref) {
	print join(",", @{$ref{$i}->{FILES}}), "\t$i $ref{$i}->{TYPE}\n";
}
