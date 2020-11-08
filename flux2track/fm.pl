sub fm {
    my $val = $_[0];
    my $fm = 0;
    for (my $i = 0x80; $i; $i >>=1) {
        my $dbit = ($val & $i) ? 1 : 0;
        $fm = ($fm << 2) | 2 | $dbit;
    }
    return $fm;
}


sub mfm {
    my ($val, $lastDbit) = @_;
    my $mfm = 0;

    for (my $i = 0x80; $i; $i >>= 1) {
        my $dbit  = ($val & $i) ? 1 : 0;
        $mfm = ($mfm << 2) | (($lastDbit | $dbit) ? 0 : 2) | $dbit;
        $lastDbit = $dbit;
    }
    return $mfm;
}

sub m2fm {
    my ($val, $lastCbit) = @_;
    my $m2fm = 0;
    my $lastDbit = 0;

    for (my $i = 0x80; $i; $i >>= 1) {
        my $dbit  = ($val & $i) ? 1 : 0;
        my $cbit = ($lastCbit | $lastDbit | $dbit) ? 0 : 1;
        $m2fm = ($m2fm << 2) | ($cbit << 1) | $dbit;
        $lastCbit = $cbit;
        $lastDbit = $dbit;
    }
    return $m2fm;
}
print $#ARGV, "\n";
print $ARGV[1], "\n";

while (($hval = shift @ARGV) ne "") {
    my $val = hex($hval);
    printf "%02X: FM %04X, MFM %04X / %04X, M2FM %04X / %04X\n", $val,
        fm($val), mfm($val, 0), mfm($val, 1), m2fm($val, 0), m2fm($val, 1);
}

