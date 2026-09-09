/********************************** (C) COPYRIGHT *******************************
 * Central_LCD JumpIAP — always enter IAP.
 *
 * DataFlash memory mapping can retain a stale ImageFlag after EEPROM_WRITE.
 * IAP uses EEPROM_READ instead, then directly enters the App when no update is
 * pending.  This follows the reliable routing model used by the WCH examples.
 *******************************************************************************/

	.section	.init,"ax",@progbits
	.global	_start
	.align	1
_start:
	j	0x6D000
