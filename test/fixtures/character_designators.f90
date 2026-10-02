module character_designator_types
  implicit none
  type :: record_t
    character(8) :: label
    character(8) :: pieces(2)
  end type
  integer :: index_calls = 0, lower_calls = 0, upper_calls = 0
contains
  integer function choose_index()
    index_calls = index_calls + 1
    choose_index = 0
  end function
  integer function choose_lower()
    lower_calls = lower_calls + 1
    choose_lower = 2
  end function
  integer function choose_upper()
    upper_calls = upper_calls + 1
    choose_upper = 5
  end function
  subroutine overwrite(text)
    character(*), intent(inout) :: text
    text = 'done'
  end subroutine
end module

program character_designators
  use character_designator_types
  implicit none
  character(8), target :: records(-1:1)
  character(8), target :: grid(-1:0,3:4)
  type(record_t), target :: data(2)
  character(:), pointer :: view
  character(4), parameter :: constant = 'abcdef'(2:5)
  character(2), parameter :: piece = constant(:2)
  character(8), parameter :: padded = 'abc'
  character(5), parameter :: padded_piece = padded(:5)
  character(2), parameter :: truncated = 'abcd'
  character(3), parameter :: binary = 'a'//achar(0)//'b'
  character(8) :: copy
  character(8) :: buffer
  integer :: status, first, last

  records(-1) = 'abcdefgh'
  records(0) = 'ijklmnop'
  records(1) = 'qrstuvwx'
  if (constant /= 'bcde' .or. piece /= 'bc') stop 1
  if (padded_piece /= 'abc  ') stop 26
  if (truncated(:) /= 'ab' .or. iachar(binary(2:2)) /= 0) stop 33
  copy = records(0)(2:5)
  if (copy /= 'jklm') stop 2
  records(0)(2:5) = records(-1)(:4)
  if (records(0) /= 'iabcdnop') stop 3
  call overwrite(records(0)(2:5))
  if (records(0) /= 'idonenop') stop 4
  view => records(0)(2:5)
  if (len(view) /= 4 .or. view /= 'done') stop 5
  view = 'view'
  if (records(0) /= 'iviewnop') stop 6
  nullify(view)
  grid = '--------'
  grid(:,:)(2:4) = reshape(['abc','def','ghi','jkl'],[2,2])
  if (any(grid(:,:)(2:4) /= reshape(['abc','def','ghi','jkl'],[2,2]))) stop 34
  grid(-1:0,3:4)(2:4) = grid(0:-1:-1,4:3:-1)(2:4)
  if (any(grid(:,:)(2:4) /= reshape(['jkl','ghi','def','abc'],[2,2]))) stop 35
  view => grid(-1,4)(2:4)
  view = 'XYZ'
  if (grid(-1,4) /= '-XYZ----') stop 36
  nullify(view)

  data(1)%label = 'abcdefgh'
  data(1)%pieces(2) = '12345678'
  data(1)%label(3:6) = data(1)%pieces(2)(:4)
  if (data(1)%label /= 'ab1234gh') stop 7
  if (data(1)%pieces(2)(5:) /= '5678') stop 8
  view => data(1)%pieces(2)(3:6)
  if (len(view) /= 4 .or. .not. associated(view, data(1)%pieces(2)(3:6))) stop 27
  view = 'ptr!'
  if (data(1)%pieces(2) /= '12ptr!78') stop 28
  nullify(view)

  first = 100
  last = -100
  copy = records(0)(first:last)
  if (copy /= '' .or. len(records(0)(first:last)) /= 0) stop 9
  if (len(records(0)(100:-100)) /= 0) stop 10
  first = -10
  last = -20
  copy = records(0)(first:last)
  if (copy /= '') stop 11

  index_calls = 0
  lower_calls = 0
  upper_calls = 0
  copy = records(choose_index())(choose_lower():choose_upper())
  if (copy /= 'view') stop 12
  if (index_calls /= 1 .or. lower_calls /= 1 .or. upper_calls /= 1) stop 13

  records(-1)(2:5) = 'ABCD'
  records(0)(2:5) = 'EFGH'
  records(1)(2:5) = 'IJKL'
  records(-1:0)(2:5) = records(0:1)(2:5)
  if (records(-1) /= 'aEFGHfgh' .or. records(0) /= 'iIJKLnop') stop 14
  records(1:-1:-1)(6:8) = 'xyz'
  if (records(-1)(6:) /= 'xyz' .or. records(0)(6:) /= 'xyz') stop 15
  if (any(records(:)(2:5) /= ['EFGH','IJKL','IJKL'])) stop 16

  data(1)%label = 'abcdefgh'
  data(2)%label = 'ijklmnop'
  data(:)%label(2:5) = ['ABCD','EFGH']
  if (data(1)%label /= 'aABCDfgh' .or. data(2)%label /= 'iEFGHnop') stop 17
  data(1)%pieces(1) = '12345678'
  data(2)%pieces(1) = 'abcdefgh'
  data(:)%pieces(1)(2:4) = ['XYZ','UVW']
  if (data(1)%pieces(1) /= '1XYZ5678') stop 18
  if (data(2)%pieces(1) /= 'aUVWefgh') stop 19

  write(buffer(2:5), '(a4)', iostat=status) records(0)(2:5)
  if (status /= 0 .or. buffer(2:5) /= 'IJKL') stop 20
  read(buffer(2:5), '(a4)', iostat=status) records(-1)(2:5)
  if (status /= 0 .or. records(-1)(2:5) /= 'IJKL') stop 21
  index_calls = 0
  lower_calls = 0
  upper_calls = 0
  records(choose_index())(choose_lower():choose_upper()) = 'side'
  if (records(0)(2:5) /= 'side') stop 22
  if (index_calls /= 1 .or. lower_calls /= 1 .or. upper_calls /= 1) stop 23
  lower_calls = 0
  upper_calls = 0
  records(:)(choose_lower():choose_upper()) = 'test'
  if (lower_calls /= 1 .or. upper_calls /= 1) stop 24
  if (any(records(:)(2:5) /= ['test','test','test'])) stop 25
  index_calls = 0
  lower_calls = 0
  upper_calls = 0
  call overwrite(records(choose_index())(choose_lower():choose_upper()))
  if (records(0)(2:5) /= 'done') stop 29
  if (index_calls /= 1 .or. lower_calls /= 1 .or. upper_calls /= 1) stop 30
  index_calls = 0
  lower_calls = 0
  upper_calls = 0
  view => records(choose_index())(choose_lower():choose_upper())
  if (view /= 'done' .or. len(view) /= 4) stop 31
  if (index_calls /= 1 .or. lower_calls /= 1 .or. upper_calls /= 1) stop 32
  nullify(view)
  write(*,'(a)') 'character designators passed'
end program
