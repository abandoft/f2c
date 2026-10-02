program character_array_actuals
  implicit none
  type :: record_t
    character(8) :: label
    character(8) :: pieces(3)
  end type
  character(8) :: records(-1:2,3:4)
  character(:), allocatable :: dynamic(:)
  type(record_t) :: data(3), selected
  character(3) :: text
  character(:), allocatable :: result_text
  integer :: result, spec_calls, markers(2)

  records = 'abcdefgh'
  call edit_matrix(records(-1:2:2,4:3:-1)(2:4))
  if (any(records(-1:2:2,:)(2:4) /= 'XYZ')) stop 1
  if (any(records(0:2:2,:) /= 'abcdefgh')) stop 2
  if (any(records(-1:2:2,:)(:1) /= 'a')) stop 3
  if (any(records(-1:2:2,:)(5:) /= 'efgh')) stop 4

  call fill_contiguous(records(2:-1:-2,3)(3:5))
  if (any(records(0:2:2,3) /= 'abOUTfgh')) stop 5
  if (any(records(0:2:2,4) /= 'abcdefgh')) stop 6
  call unspecified(records(-1:2:2,3)(6:8))
  if (any(records(-1:2:2,3) /= 'aXYZeabc')) stop 7

  data%label = 'abcdefgh'
  data(2)%pieces = '12345678'
  call fill_contiguous(data(3:1:-2)%label(3:5))
  if (data(1)%label /= 'abOUTfgh' .or. data(2)%label /= 'abcdefgh') stop 8
  if (data(3)%label /= 'abOUTfgh') stop 9
  call unspecified(data(2)%pieces(3:1:-2)(2:4))
  if (data(2)%pieces(1) /= '1abc5678' .or. data(2)%pieces(3) /= '1abc5678') stop 10
  if (data(2)%pieces(2) /= '12345678') stop 11

  allocate(character(7) :: dynamic(-2:1))
  dynamic = 'abcdefg'
  call fill_contiguous(dynamic(1:-2:-2)(4:6))
  if (dynamic(-1) /= 'abcOUTg' .or. dynamic(1) /= 'abcOUTg') stop 12
  if (dynamic(-2) /= 'abcdefg' .or. dynamic(0) /= 'abcdefg') stop 13

  call inspect_vector(dynamic([1,-1,1])(4:6), 'OUT')
  call inspect_vector(data([3,1,3])%label(3:5), 'OUT')
  call inspect_empty(dynamic(1:-2)(100:-100))
  call inspect_empty(dynamic(:)(100:-100))

  call fill_contiguous(records(0,3:4)(2:4))
  if (records(0,4) /= 'aOUTefgh') stop 15

  records = 'abcdefgh'
  call edit_explicit(records(-1:2:2,3)(2:4))
  if (any(records(-1:2:2,3) /= 'aRAWefgh')) stop 16
  if (any(records(0:2:2,3) /= 'abcdefgh')) stop 17
  if (.not. matches(records(-1:2:2,3)(2:4), 'RAW')) stop 18
  result = edit_count(records(2:-1:-2,4)(3:5))
  if (result /= 2 .or. any(records(0:2:2,4) /= 'abFUNfgh')) stop 19
  result = count_explicit(data(3:1:-2)%label(3:5))
  if (result /= 2) stop 20
  if (.not. matches_contiguous(dynamic(1:-2:-2)(4:6), 'OUT')) stop 27
  text = first_text(dynamic(1:-2:-2)(4:6))
  if (text /= 'OUT') stop 28
  selected = first_record(dynamic(1:-2:-2)(4:6))
  if (selected%label /= 'OUT' .or. any(selected%pieces /= 'OUT')) stop 29
  spec_calls = 0
  result_text = sized_text(n=choose_n(), values=dynamic(1:-2:-2)(4:6))
  if (len(result_text) /= 7 .or. result_text /= 'OUT!   ') stop 30
  if (spec_calls /= 1) stop 32
  result_text = sized_text(dynamic, choose_n())
  if (spec_calls /= 2 .or. len(result_text) /= 13) stop 33
  if (result_text(:8) /= 'abcdefg!') stop 34
  result_text = sized_text(dynamic(:)(100:-100), 2)
  if (len(result_text) /= 6 .or. result_text /= '!     ') stop 31
  markers = 10
  result = edit_mark(records(-1:2:2,4)(2:4), markers(2))
  if (result /= 2 .or. markers(1) /= 10 .or. markers(2) /= 11) stop 35
  if (any(records(-1:2:2,4) /= 'aMUTefgh')) stop 36
  deallocate(result_text)
  deallocate(dynamic)
  print *, 'character array actuals passed'
contains
  subroutine edit_matrix(values)
    character(*), intent(inout) :: values(:,:)
    if (len(values) /= 3 .or. any(values /= 'bcd')) stop 21
    values = 'XYZ'
  end subroutine
  subroutine fill_contiguous(values)
    character(*), intent(out), contiguous :: values(:)
    if (len(values) /= 3) stop 22
    values = 'OUT'
  end subroutine
  subroutine unspecified(values)
    character(*) :: values(:)
    values = 'abc'
  end subroutine
  subroutine inspect_vector(values, expected)
    character(*), intent(in) :: values(:), expected
    if (size(values) /= 3 .or. any(values /= expected)) stop 23
  end subroutine
  subroutine inspect_empty(values)
    character(*), intent(in) :: values(:)
    if (size(values) /= 0 .and. len(values) /= 0) stop 24
  end subroutine
  subroutine edit_explicit(values)
    character(*), intent(inout) :: values(2)
    if (any(values /= 'bcd')) stop 25
    values = 'RAW'
  end subroutine
  logical function matches(values, expected)
    character(*), intent(in) :: values(:), expected
    matches = all(values == expected)
  end function
  logical function matches_contiguous(values, expected)
    character(*), intent(in), contiguous :: values(:)
    character(*), intent(in) :: expected
    matches_contiguous = all(values == expected)
  end function
  function first_text(values) result(text)
    character(*), intent(in) :: values(:)
    character(len=len(values)) :: text
    text = values(1)
  end function
  function first_record(values) result(item)
    character(*), intent(in) :: values(:)
    type(record_t) :: item
    item%label = values(1)
    item%pieces = values(1)
  end function
  function sized_text(values, n) result(text)
    character(*), intent(in) :: values(:)
    integer, intent(in) :: n
    character(len=n+len(values)+size(values)) :: text
    text = values(1)//'!'
  end function
  integer function edit_count(values)
    character(*), intent(inout) :: values(:)
    values = 'FUN'
    edit_count = size(values)
  end function
  integer function edit_mark(values, marker)
    character(*), intent(inout) :: values(:)
    integer, intent(inout) :: marker
    values = 'MUT'
    marker = marker + 1
    edit_mark = size(values)
  end function
  integer function choose_n()
    spec_calls = spec_calls + 1
    choose_n = 2
  end function
  integer function count_explicit(values)
    character(*), intent(in) :: values(2)
    if (any(values /= 'OUT')) stop 26
    count_explicit = size(values)
  end function
end program
