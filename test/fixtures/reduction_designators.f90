program reduction_designators
  implicit none
  type :: item_t
    logical :: enabled
    integer :: value
    complex(8) :: phase
  end type
  type(item_t) :: data(2,3)
  logical, target :: flags(-1:2,3:5)
  logical, pointer :: selection(:,:)
  integer :: calls

  data%enabled = .true.
  data%value = 2
  data%phase = cmplx(1.0_8, 2.0_8, kind=8)
  if (.not. all(data%enabled)) stop 1
  if (.not. any(data%enabled)) stop 2
  if (count(data%enabled) /= 6) stop 3
  if (sum(data%value) /= 12 .or. product(data%value) /= 64) stop 4
  if (minval(data%value) /= 2 .or. maxval(data%value) /= 2) stop 5
  if (dot_product(data(:,1)%value, data(:,2)%value) /= 8) stop 6
  if (abs(sum(data%phase)-cmplx(6.0_8,12.0_8,kind=8)) > 0.0_8) stop 7
  if (count(data(2:1:-1,3:1:-2)%enabled) /= 4) stop 8
  if (.not. all(data(2:1,:)%enabled)) stop 9
  if (any(data(2:1,:)%enabled) .or. count(data(2:1,:)%enabled) /= 0) stop 10
  if (sum(data(2:1,:)%value) /= 0 .or. product(data(2:1,:)%value) /= 1) stop 11

  calls = 0
  if (count(data(first_row():1:-1,:)%enabled) /= 6) stop 12
  if (calls /= 1) stop 13

  flags = .true.
  flags(0,4) = .false.
  selection => flags(2:-1:-2,5:3:-1)
  if (all(selection) .or. .not. any(selection)) stop 14
  if (count(selection) /= 5) stop 15
  call inspect(flags(2:-1:-2,5:3:-1))
  if (count(flags(2:-1:-2,5:3:-1)) /= 5) stop 16
  nullify(selection)
  print *, 'reduction designators passed'

contains

  integer function first_row()
    calls = calls + 1
    first_row = 2
  end function

  subroutine inspect(values)
    logical, intent(in) :: values(:,:)
    if (count(values) /= 5) stop 17
    if (all(values) .or. .not. any(values)) stop 18
  end subroutine
end program
