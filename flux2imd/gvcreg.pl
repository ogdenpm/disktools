sub make_reg
{
	my($crc16, $bits, $bit16, $i) = (0, $_[0]);

	for ($i = 0; $i < 32; $i++) {
		$bit16 = $crc16 & 1;
		$crc16 = (($crc16 >> 1) + (($bits & 1) << 15));
		$crc16 ^= 0x8408 if $bit16 == 1;
		$bits >>= 1;
	}
	return $crc16;
}

$n = <STDIN>;
print make_reg($n), "\n";

