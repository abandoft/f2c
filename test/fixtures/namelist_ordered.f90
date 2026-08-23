program namelist_ordered
  implicit none

  integer :: scalar
  integer :: values(4)
  character(8) :: title
  character(4) :: words(3)
  logical :: flags(3)
  complex :: phases(2)
  integer :: status
  character(512) :: record
  namelist /sample/ scalar, values, title, words, flags, phases

  scalar = 0
  values = 0
  title = 'abcdef'
  words = ['old1', 'old2', 'old3']
  flags = [.false., .false., .false.]
  phases = [(0.0, 0.0), (0.0, 0.0)]
  record = "&sample scalar=1, values=2,3,4,5, scalar=9, values=2*6,,10, " // &
           "values(2:4:2)=20,40, values(3)=30, words=2*'xy',, " // &
           "words(1:3:2)='aa','cc', title(2:4)='XYZ', words(2)(2:3)='zz', " // &
           "flags=2*.true.,, phases=2*(1.0,2.0) /"
  read(record, nml=sample, iostat=status)

  if (status /= 0) stop 1
  if (scalar /= 9) stop 2
  if (any(values /= [6, 20, 30, 40])) stop 3
  if (title /= 'aXYZef') stop 4
  if (any(words /= ['aa  ', 'xzz ', 'cc  '])) stop 5
  if (any(flags .neqv. [.true., .true., .false.])) stop 6
  if (abs(phases(1) - cmplx(1.0, 2.0)) > 1.0e-6) stop 7
  if (abs(phases(2) - cmplx(1.0, 2.0)) > 1.0e-6) stop 8
  write(*, '(A)') 'ordered-ok'
end program namelist_ordered
