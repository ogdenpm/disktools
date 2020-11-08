sub flip {
    my $ival = $_[0];
    my $val = 0;
    for(my $i = 0x80; $i; $i >>= 1) {
        $val = ($val >> 1) + (($ival & $i) ? 0x80 : 0);
    }
    return $val;
}


for (my $i = 0; $i < 256; $i++) {
    printf " 0x%02X,", flip($i);
    print "\n" if ($i % 16 == 15);
}
