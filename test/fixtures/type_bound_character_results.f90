module bound_character_result_storage
  implicit none
  integer :: body_calls = 0
  integer :: index_calls = 0
  type :: text_box
    integer :: width = 2
  contains
    procedure, pass(self) :: make => make_text
    procedure, pass(self) :: first => make_first
    procedure, pass(self) :: last => make_last
    procedure, pass(self) :: update => update_box
    procedure, pass(self) :: edit => edit_text
    procedure, nopass :: bare => make_bare
  end type
  type, extends(text_box) :: child_box
    integer :: extra = 7
  contains
    procedure, pass(self) :: make => make_child_text
  end type
  type(text_box) :: global
contains
  integer function object_index()
    index_calls = index_calls+1
    object_index = 2
  end function
  function edit_text(values, self) result(text)
    character(*), intent(inout), contiguous :: values(:)
    class(text_box), intent(inout) :: self
    character(len(values)+self%width) :: text
    body_calls = body_calls+1
    text = 'e'
    values = 'uv'
    self%width = self%width+1
  end function
  function make_text(n, self) result(text)
    integer, intent(inout) :: n
    class(text_box), intent(inout) :: self
    character(n + self%width) :: text
    body_calls = body_calls + 1
    text = 'x'
    n = n + 3
    self%width = self%width + 4
  end function
  function make_first(self, n) result(text)
    class(text_box), intent(inout) :: self
    integer, intent(inout) :: n
    character(n) :: text
    body_calls = body_calls + 1
    text = 'z'
    n = n + 2
    self%width = self%width + 1
  end function
  function make_child_text(n, self) result(text)
    integer, intent(inout) :: n
    class(child_box), intent(inout) :: self
    character(n + self%width) :: text
    body_calls = body_calls+1
    text = 'c'
    n = n+2
    self%width = self%width+1
  end function
  function make_bare(n, extra) result(text)
    integer, intent(inout) :: n
    integer, optional, intent(in) :: extra
    character(n) :: text
    body_calls = body_calls + 1
    text = 'y'
    if (present(extra)) text = 'w'
    n = n + 3
  end function
  subroutine update_box(n, self, extra)
    integer, intent(inout) :: n
    class(text_box), intent(inout) :: self
    integer, optional, intent(in) :: extra
    n = n+1
    if (present(extra)) self%width = extra
  end subroutine
  function make_last(n, extra, self) result(text)
    integer, intent(inout) :: n, extra
    class(text_box), intent(inout) :: self
    character(n + extra + self%width) :: text
    body_calls = body_calls + 1
    text = 'l'
    n = n + 1
    extra = extra + 2
    self%width = self%width + 3
  end function
  subroutine consume(text)
    character(*), intent(in) :: text
    if (len(text) /= 2 .or. text /= 'y ') stop 15
  end subroutine
  subroutine verify_dynamic(self)
    class(text_box), intent(inout) :: self
    integer :: n
    character(:), allocatable :: text
    n = 1
    text = self%make(n)
    if (text /= 'c  ' .or. len(text) /= 3 .or. n /= 3 .or. self%width /= 3) stop 22
    deallocate(text)
  end subroutine
end module
program type_bound_character_results
  use bound_character_result_storage
  implicit none
  type(text_box) :: box, boxes(2)
  type(child_box) :: concrete
  character(:), allocatable :: text
  character(5) :: fixed
  character(2) :: pairs(2)
  character(4) :: words(3)
  integer :: n, extra
  n = 1
  text = box%make(n)
  if (text /= 'x  ' .or. len(text) /= 3 .or. n /= 4 .or. box%width /= 6) stop 1
  n = 2
  text = box%bare(n=n)
  if (text /= 'y ' .or. len(text) /= 2 .or. n /= 5) stop 2
  n = 2
  text = box%first(n=n)
  if (text /= 'z ' .or. len(text) /= 2 .or. n /= 4 .or. box%width /= 7) stop 3
  n = -7
  text = box%make(n=n)
  if (len(text) /= 0 .or. n /= -4 .or. box%width /= 11) stop 4
  n = 2
  text = box%bare(n) // 'q'
  if (text /= 'y q' .or. len(text) /= 3 .or. n /= 5) stop 5
  n = 2
  if (box%bare(n) /= 'y ') stop 6
  if (n /= 5) stop 7
  n = 2
  fixed = box%bare(n)
  if (fixed /= 'y    ' .or. n /= 5) stop 8
  n = 0
  pairs = box%bare(n)
  if (any(pairs /= '  ') .or. n /= 3) stop 9
  n = 1
  text = boxes(object_index())%make(n)
  if (text /= 'x  ' .or. n /= 4 .or. boxes(1)%width /= 2 .or. boxes(2)%width /= 6) stop 10
  if (index_calls /= 1) stop 19
  n = 1
  text = global%make(n)
  if (text /= 'x  ' .or. n /= 4 .or. global%width /= 6) stop 11
  n = 2
  call consume(box%bare(n))
  if (n /= 5 .or. body_calls /= 11) stop 12
  n = 1
  extra = 2
  text = boxes(1)%last(extra=extra, n=n)
  if (text /= 'l    ' .or. len(text) /= 5 .or. n /= 2 .or. extra /= 4) stop 13
  if (boxes(1)%width /= 5 .or. body_calls /= 12) stop 14
  n = 1
  extra = 8
  call box%update(extra=extra, n=n)
  if (n /= 2 .or. box%width /= 8) stop 16
  call box%update(n)
  if (n /= 3 .or. box%width /= 8) stop 17
  n = 2
  text = box%bare(extra=extra, n=n)
  if (text /= 'w ' .or. n /= 5 .or. body_calls /= 13) stop 18
  words = ['abcd','efgh','ijkl']
  box%width = 2
  text = box%edit(words(3:1:-2)(2:3))
  if (text /= 'e   ' .or. len(text) /= 4 .or. box%width /= 3 .or. body_calls /= 14) stop 20
  if (any(words /= ['auvd','efgh','iuvl'])) stop 21
  call verify_dynamic(concrete)
  if (body_calls /= 15) stop 23
  deallocate(text)
  print '(a)', 'type-bound character results passed'
end program
