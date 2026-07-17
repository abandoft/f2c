module where_counter
  implicit none
  type :: cell
    integer :: value
  end type cell
  integer :: threshold_calls, bound_calls, constructor_calls, character_constructor_calls
  integer :: derived_constructor_calls
contains
  integer function threshold()
    threshold_calls = threshold_calls + 1
    threshold = 0
  end function threshold

  integer function upper_bound()
    bound_calls = bound_calls + 1
    upper_bound = 6
  end function upper_bound

  integer function scalar_value()
    scalar_value = 7
  end function scalar_value

  character(len=3) function scalar_word()
    scalar_word = 'new'
  end function scalar_word

  integer function constructor_value(value)
    integer, intent(in) :: value
    constructor_calls = constructor_calls + 1
    constructor_value = value
  end function constructor_value

  character(len=3) function constructor_word(value)
    character(len=3), intent(in) :: value
    character_constructor_calls = character_constructor_calls + 1
    constructor_word = value
  end function constructor_word

  type(cell) function constructor_cell(value)
    integer, intent(in) :: value
    derived_constructor_calls = derived_constructor_calls + 1
    constructor_cell = cell(value)
  end function constructor_cell
end module where_counter

program where_construct
  use where_counter
  implicit none
  integer :: values(6), probe(4), constructor_source(4), i, n
  integer :: matrix(2,3)
  logical :: mask(6)
  logical :: probe_mask(4)
  logical :: cell_mask(4)
  character(len=3) :: words(6), character_source(4)
  type(cell) :: cells(4), replacements(4)
  integer, allocatable :: dynamic_values(:)
  logical, allocatable :: dynamic_mask(:)

  values = [-3, -1, 0, 1, 2, 3]
  words = 'old'

  classify: where (values > 0)
    values = -values
    values = values - 10
    words = 'pos'
  elsewhere (values < -1) classify
    values = -values
    words = 'neg'
  elsewhere classify
    values = 99
    words = 'zer'
  end where classify

  if (any(values /= [3, 99, 99, -11, -12, -13])) error stop 1
  if (any(words /= ['neg', 'zer', 'zer', 'pos', 'pos', 'pos'])) error stop 2

  mask = [.false., .true., .true., .true., .true., .true.]
  n = 5
  shifted: where (mask(2:n + 1))
    values(2:n + 1) = values(1:n)
  end where shifted
  if (any(values /= [3, 3, 99, 99, -11, -12])) error stop 3

  outer: where (values > 0)
    inner: where (mod(values, 2) == 0)
      values = values / 2
    elsewhere inner
      values = values + 1
    end where inner
  end where outer
  if (any(values /= [4, 4, 100, 100, -11, -12])) error stop 4

  where (values == 100) values = -100
  if (any(values /= [4, 4, -100, -100, -11, -12])) error stop 5

  mask = .true.
  where (mask) values = values(6:1:-1)
  if (any(values /= [-12, -11, -100, -100, 4, 4])) error stop 6

  matrix = reshape([1, 2, 3, 4, 5, 6], [2, 3])
  where (matrix > 2)
    matrix = matrix + 10
  elsewhere
    matrix = -matrix
  end where
  if (any(matrix /= reshape([-1, -2, 13, 14, 15, 16], [2, 3]))) error stop 7

  do i = 1, 4
    cells(i)%value = i
    replacements(i)%value = 10 * i
  end do
  cell_mask = [.false., .true., .false., .true.]
  where (cell_mask) cells = replacements
  if (cells(1)%value /= 1 .or. cells(2)%value /= 20 .or. &
      cells(3)%value /= 3 .or. cells(4)%value /= 40) error stop 8

  allocate(dynamic_values(0:4), dynamic_mask(0:4))
  dynamic_values = [1, 2, 3, 4, 5]
  do i = 0, 4
    dynamic_mask(i) = dynamic_values(i) > 2
  end do
  where (dynamic_mask) dynamic_values = dynamic_values * 3
  if (any(dynamic_values /= [1, 2, 9, 12, 15])) error stop 9
  deallocate(dynamic_values, dynamic_mask)

  allocate(dynamic_values(1:0), dynamic_mask(1:0))
  where (dynamic_mask)
    dynamic_values = 1
  elsewhere
    dynamic_values = 2
  end where
  deallocate(dynamic_values, dynamic_mask)

  probe = [-1, 1, 2, -2]
  threshold_calls = 0
  where (probe > threshold()) probe = probe + 10
  if (threshold_calls /= 1) error stop 10
  if (any(probe /= [-1, 11, 12, -2])) error stop 11

  bound_calls = 0
  mask = .true.
  where (mask(2:upper_bound())) values(1:5) = values(1:5)
  if (bound_calls /= 1) error stop 12

  probe_mask = [.true., .false., .true., .false.]
  where (probe_mask) probe = scalar_value()
  if (any(probe /= [7, 11, 7, -2])) error stop 13

  where (probe_mask) words(1:4) = scalar_word()
  if (any(words(1:4) /= ['new', 'zer', 'new', 'pos'])) error stop 14

  constructor_calls = 0
  where (probe_mask) probe = [constructor_value(21), constructor_value(22), &
                              constructor_value(23), constructor_value(24)]
  if (constructor_calls /= 4) error stop 15
  if (any(probe /= [21, 11, 23, -2])) error stop 16

  constructor_calls = 0
  where (probe_mask) probe = [(constructor_value(i), i = 31, 34)]
  if (constructor_calls /= 4) error stop 17
  if (any(probe /= [31, 11, 33, -2])) error stop 18

  constructor_calls = 0
  where (probe_mask) probe = [[constructor_value(41), constructor_value(42)], &
                              [constructor_value(43), constructor_value(44)]]
  if (constructor_calls /= 4) error stop 19
  if (any(probe /= [41, 11, 43, -2])) error stop 20

  constructor_source = [51, 52, 53, 54]
  where (probe_mask) probe = [constructor_source]
  if (any(probe /= [51, 11, 53, -2])) error stop 21

  character_constructor_calls = 0
  where (probe_mask) words(1:4) = [(constructor_word('imp'), i = 1, 4)]
  if (character_constructor_calls /= 4) error stop 22
  if (any(words(1:4) /= ['imp', 'zer', 'imp', 'pos'])) error stop 23

  character_constructor_calls = 0
  where (probe_mask) words(1:4) = [[constructor_word('c01'), constructor_word('c02')], &
                                   [constructor_word('c03'), constructor_word('c04')]]
  if (character_constructor_calls /= 4) error stop 24
  if (any(words(1:4) /= ['c01', 'zer', 'c03', 'pos'])) error stop 25

  character_source = ['s01', 's02', 's03', 's04']
  where (probe_mask) words(1:4) = [character_source]
  if (any(words(1:4) /= ['s01', 'zer', 's03', 'pos'])) error stop 26

  derived_constructor_calls = 0
  where (cell_mask) cells = [(constructor_cell(i), i = 101, 104)]
  if (derived_constructor_calls /= 4) error stop 27
  if (cells(1)%value /= 1 .or. cells(2)%value /= 102 .or. &
      cells(3)%value /= 3 .or. cells(4)%value /= 104) error stop 28

  derived_constructor_calls = 0
  where (cell_mask) cells = [[constructor_cell(201), constructor_cell(202)], &
                              [constructor_cell(203), constructor_cell(204)]]
  if (derived_constructor_calls /= 4) error stop 29
  if (cells(1)%value /= 1 .or. cells(2)%value /= 202 .or. &
      cells(3)%value /= 3 .or. cells(4)%value /= 204) error stop 30

  where (cell_mask) cells = [replacements]
  if (cells(1)%value /= 1 .or. cells(2)%value /= 20 .or. &
      cells(3)%value /= 3 .or. cells(4)%value /= 40) error stop 31

  do i = 1, 6
    write (*, '(I0,1X,A)') values(i), words(i)
  end do
end program where_construct
