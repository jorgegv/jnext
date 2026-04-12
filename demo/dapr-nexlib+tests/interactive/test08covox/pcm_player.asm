PUBLIC _jump_start
PUBLIC _jump_end
PUBLIC _death_start
PUBLIC _death_end


; SECTION PAGE_90

; _mothership_start:
; BINARY "res/samples/mothership.pcm"
; _mothership_end:

; SECTION PAGE_91

; _explode_start:
; BINARY "res/samples/explode.pcm"
; _explode_end:

SECTION PAGE_94

ORG $0000

_death_start:
INCBIN "res/samples/death.raw"
_death_end:

_jump_start:
INCBIN "res/samples/spring.raw"
_jump_end:

