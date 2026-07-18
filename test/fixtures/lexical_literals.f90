program lexical_literals
  implicit none
  integer, parameter :: character_kind = 1
  character(kind=character_kind, len=8) :: continued
  character(kind=1, len=3) :: prefixed
  character(len=4) :: legacy

  continued = character_kind_'Ab!;&
&CdEf'
  prefixed = 1_'XyZ'
  legacy = 4H!;Az

  if (continued /= character_kind_'Ab!;CdEf') error stop 1
  if (prefixed /= 1_'XyZ') error stop 2
  if (legacy /= '!;Az') error stop 3
  write (*, '(a)') continued
  write (*, '(a)') prefixed
  write (*, '(a)') legacy
end program lexical_literals
