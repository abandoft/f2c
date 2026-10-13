program byte_view_values
  implicit none
  type :: inner
    real(8) :: number
    complex(8) :: phase
    character(5) :: text
  end type
  type :: entry
    integer :: guard
    type(inner) :: payload
  end type
  type(entry) :: records(2,3)
  integer :: i, j
  records%guard = 73
  records%payload%number = 2.0_8
  records%payload%phase = cmplx(1.0_8,2.0_8,kind=8)
  records%payload%text = 'abcde'
  call edit_numbers(records%payload%number)
  call edit_phases(records(2:1:-1,3:1:-1)%payload%phase)
  call edit_text(records%payload%text(2:3))
  do j = 1, 3
    do i = 1, 2
      if (records(i,j)%guard /= 73) stop 1
      if (records(i,j)%payload%number /= 6.0_8) stop 2
      if (records(i,j)%payload%phase /= cmplx(4.0_8,6.0_8,kind=8)) stop 3
      if (records(i,j)%payload%text /= 'aXYde') stop 4
    end do
  end do
  print '(a)', 'byte view values passed'
contains
  subroutine edit_numbers(values)
    real(8), intent(inout) :: values(:,:)
    if (sum(values) /= 12.0_8) stop 5
    values = values * 3.0_8
    if (sum(values) /= 36.0_8) stop 6
  end subroutine
  subroutine edit_phases(values)
    complex(8), intent(inout) :: values(:,:)
    values = values + cmplx(3.0_8,4.0_8,kind=8)
    if (sum(values) /= cmplx(24.0_8,36.0_8,kind=8)) stop 7
  end subroutine
  subroutine edit_text(values)
    character(*), intent(inout) :: values(:,:)
    if (len(values) /= 2 .or. any(values /= 'bc')) stop 8
    values = 'XY'
    if (any(values /= 'XY')) stop 9
  end subroutine
end program
