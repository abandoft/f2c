program parameter_components
  implicit none
  type :: payload
    integer :: values(2) = reshape([4,8],[2])
    character(len=3) :: labels(2) = ['AA','BB']
  end type
  integer, parameter :: source_values(2) = [11,17]
  character(len=2), parameter :: source_labels(2) = ['X ','YZ']
  type(payload), parameter :: explicit_value = payload(values=reshape(source_values,[2]), &
                                                       labels=source_labels)
  type(payload), parameter :: default_value = payload()
  type(payload), parameter :: values(2) = [explicit_value, default_value]
  type(payload), parameter :: reversed(2) = cshift(values,1)
  integer :: position
  if (any(explicit_value%values /= [11,17])) stop 1
  if (explicit_value%labels(1) /= 'X  ' .or. explicit_value%labels(2) /= 'YZ ') stop 2
  if (any(default_value%values /= [4,8])) stop 3
  if (default_value%labels(1) /= 'AA ' .or. default_value%labels(2) /= 'BB ') stop 4
  do position = 1,2
    if (values(1)%values(position) /= source_values(position)) stop 5
    if (reversed(2)%labels(position) /= explicit_value%labels(position)) stop 6
    if (reversed(1)%values(position) /= default_value%values(position)) stop 7
  end do
  print '(A)', 'typed constant component arrays, defaults and parameter references passed'
end program
