program scoped_unaligned_storage
  implicit none
  integer, volatile :: words(5)
  double precision, volatile :: values(2)
  double precision :: snapshot(2)
  character(len=32) :: buffer
  character(len=128) :: namelist_buffer
  equivalence (words(2), values(1))
  namelist /observed/ values
  values = [1.25d0, 2.75d0]
  call ordinary_scalar(values(2))
  call ordinary_array(values)
  if (any(values /= [1.5d0, 3.5d0])) stop 1
  snapshot = values
  values = snapshot + 1.0d0
  if (any(values /= [2.5d0, 4.5d0])) stop 2
  write(buffer, '(2F8.3)') values
  values = 0.0d0
  read(buffer, '(2F8.3)') values
  if (any(values /= [2.5d0, 4.5d0])) stop 3
  write(namelist_buffer, nml=observed)
  values = -1.0d0
  read(namelist_buffer, nml=observed)
  if (any(values /= [2.5d0, 4.5d0])) stop 4
  print '(A)', 'scoped unaligned storage contracts passed'
contains
  subroutine ordinary_scalar(value)
    double precision, intent(inout) :: value
    value = value + 0.5d0
  end subroutine
  subroutine ordinary_array(array)
    double precision, intent(inout) :: array(2)
    array = array + 0.25d0
  end subroutine
end program
