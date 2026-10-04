program lexical_literals
  implicit none
  integer, parameter :: character_kind = 1
  character(kind=character_kind, len=8) :: continued
  character(kind=1, len=3) :: prefixed
  character(len=4) :: legacy
  logical(kind=1) :: narrow
  logical(kind=8) :: wide
  logical(kind=2), parameter :: true2 = .true._2, false2 = .false._2

  continued = character_kind_'Ab!;&
&CdEf'
  prefixed = 1_'XyZ'
  legacy = 4H!;Az
  narrow = .true._character_kind
  wide = .false._8
  if (.not. narrow .or. wide) error stop 4
  if (.not. true2 .or. false2) error stop 5
  if (kind(.true._character_kind) /= 1) error stop 6
  if (kind(.not. narrow) /= 1) error stop 7

  if (continued /= character_kind_'Ab!;CdEf') error stop 1
  if (prefixed /= 1_'XyZ') error stop 2
  if (legacy /= '!;Az') error stop 3
  write (*, '(a)') continued
  write (*, '(a)') prefixed
  write (*, '(a)') legacy
end program lexical_literals
