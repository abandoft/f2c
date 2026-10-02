! F2018 10.1.9.2 and 15.5.2: a parenthesized actual is a value,
! not an association with the original character storage.
program parenthesized_character_values
  implicit none
  character(len=5), volatile :: text
  character(len=3) :: words(-1:0)
  character(len=:), allocatable :: dynamic_text
  character(len=0) :: empty
  integer :: evaluations

  text = 'abcde'
  call check_text(((text)))
  if (text /= 'vwxyz') stop 1
  words = ['one','two']
  call check_words((words))
  if (any(words /= ['new','new'])) stop 2
  words = ['one','two']
  call check_reverse((words(0:-1:-1)))
  dynamic_text = 'initial'
  call check_dynamic((dynamic_text))
  if (dynamic_text /= 'changed length') stop 3
  call check_empty((empty))
  evaluations = 0
  call check_function(((make_text())))
  if (evaluations /= 1) stop 4
  text = 'a' // achar(0) // 'bcd'
  call check_bytes((text))
  print '(A)', 'parenthesized character contracts passed'
contains
  subroutine check_text(value)
    character(len=*), intent(in) :: value
    text = 'vwxyz'
    if (len(value) /= 5 .or. value /= 'abcde') stop 10
  end subroutine
  subroutine check_words(value)
    character(len=*), intent(in) :: value(:)
    words = 'new'
    if (len(value) /= 3 .or. any(value /= ['one','two'])) stop 11
  end subroutine
  subroutine check_reverse(value)
    character(len=*), intent(in) :: value(2)
    words = 'new'
    if (any(value /= ['two','one'])) stop 12
  end subroutine
  subroutine check_dynamic(value)
    character(len=*), intent(in) :: value
    dynamic_text = 'changed length'
    if (len(value) /= 7 .or. value /= 'initial') stop 13
  end subroutine
  subroutine check_empty(value)
    character(len=*), intent(in) :: value
    if (len(value) /= 0 .or. value /= '') stop 14
  end subroutine
  function make_text() result(value)
    character(len=4) :: value
    evaluations = evaluations + 1
    value = 'once'
  end function
  subroutine check_function(value)
    character(len=*), intent(in) :: value
    if (len(value) /= 4 .or. value /= 'once') stop 15
  end subroutine
  subroutine check_bytes(value)
    character(len=*), intent(in) :: value
    text = 'other'
    if (len(value) /= 5) stop 16
    if (value /= 'a' // achar(0) // 'bcd') stop 17
  end subroutine
end program
