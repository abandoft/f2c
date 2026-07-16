program namelist_internal
  implicit none
  character(len=256) :: record
  character(len=8) :: name
  integer :: count
  real :: scale
  logical :: enabled
  namelist /state/ count, scale, enabled, name

  count = 42
  scale = 1.25
  enabled = .true.
  name = 'sample'
  write(record, nml=state)

  count = 0
  scale = 0.0
  enabled = .false.
  name = 'missing'
  read(record, nml=state)

  if (count /= 42 .or. abs(scale - 1.25) > 1.0e-6 .or. .not. enabled .or. &
      name /= 'sample') stop 1
end program namelist_internal
