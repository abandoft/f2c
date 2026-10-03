subroutine fetch_scalar(output)
  integer, intent(out) :: output
  interface
    function borrowed_scalar() result(value)
      integer, pointer :: value
    end function
  end interface
  output = borrowed_scalar()
end subroutine

subroutine allocate_from_results(output, characters)
  integer,intent(out) :: output(3)
  character(len=*),intent(out) :: characters
  integer,allocatable :: first(:), second(:)
  character(len=:),allocatable :: text
  interface
    function owned_scalar() result(value)
      integer,allocatable :: value
    end function
    function owned_character() result(value)
      character(len=:),allocatable :: value
    end function
  end interface
  allocate(first(3), second(3), source=owned_scalar())
  allocate(text, source=owned_character())
  output = first + second
  characters = text
  deallocate(first, second, text)
end subroutine

subroutine allocate_from_mold(output)
  integer,intent(out) :: output(2)
  integer,allocatable :: target(:)
  interface
    function borrowed_array() result(value)
      integer,pointer :: value(:)
    end function
  end interface
  allocate(target, mold=borrowed_array())
  output(1) = size(target)
  output(2) = lbound(target,1)
  deallocate(target)
end subroutine

subroutine allocate_result_failure(status)
  integer,intent(out) :: status
  integer,allocatable :: target
  interface
    function owned_scalar() result(value)
      integer,allocatable :: value
    end function
  end interface
  allocate(target, source=owned_scalar(), stat=status)
end subroutine

subroutine allocate_control_results(output)
  integer, intent(out) :: output(6)
  integer, allocatable :: first(:), second(:)
  integer :: statuses(3)
  character(len=40) :: messages(3)
  interface
    function owned_control(n) result(value)
      integer, intent(in) :: n
      integer, allocatable :: value
    end function
  end interface
  statuses = -99
  messages = 'untouched'
  allocate(first(owned_control(2):owned_control(4)), second(owned_control(3)), &
           source=9, stat=statuses(owned_control(2)), &
           errmsg=messages(owned_control(2))(owned_control(2):20))
  output = [statuses(2), size(first), lbound(first,1), sum(first), size(second), &
            merge(1,0,all(messages == 'untouched'))]
  deallocate(first, second)
end subroutine

subroutine allocate_length_results(output)
  integer, intent(out) :: output(3)
  integer :: statuses(3)
  character(len=:), allocatable :: first, second
  interface
    function owned_control(n) result(value)
      integer, intent(in) :: n
      integer, allocatable :: value
    end function
  end interface
  statuses = -99
  allocate(character(len=owned_control(4)) :: first, second, &
           stat=statuses(owned_control(2)))
  output = [statuses(2), len(first), len(second)]
  deallocate(first, second)
end subroutine

subroutine allocate_control_failure(status)
  integer, intent(out) :: status
  integer, allocatable :: target(:)
  interface
    function owned_control(n) result(value)
      integer, intent(in) :: n
      integer, allocatable :: value
    end function
  end interface
  allocate(target(owned_control(3)), stat=status)
end subroutine

subroutine allocate_control_guard(status)
  integer, intent(out) :: status
  integer :: statuses(3)
  integer, allocatable :: target, missing
  interface
    function owned_control(n) result(value)
      integer, intent(in) :: n
      integer, allocatable :: value
    end function
  end interface
  statuses = -99
  allocate(target, source=missing, stat=statuses(owned_control(2)))
  status = statuses(2)
end subroutine

subroutine allocate_many_controls(output)
  integer, intent(out) :: output
  integer, allocatable :: a(:), b(:), c(:), d(:), e(:), f(:), g(:), h(:), i(:)
  integer, allocatable :: j(:), k(:), l(:), m(:), n(:), o(:), p(:), q(:)
  interface
    function owned_control(n) result(value)
      integer, intent(in) :: n
      integer, allocatable :: value
    end function
  end interface
  allocate(a(owned_control(1)), b(owned_control(1)), c(owned_control(1)), &
           d(owned_control(1)), e(owned_control(1)), f(owned_control(1)), &
           g(owned_control(1)), h(owned_control(1)), i(owned_control(1)), &
           j(owned_control(1)), k(owned_control(1)), l(owned_control(1)), &
           m(owned_control(1)), n(owned_control(1)), o(owned_control(1)), &
           p(owned_control(1)), q(owned_control(1)), source=7)
  output = sum(a)+sum(b)+sum(c)+sum(d)+sum(e)+sum(f)+sum(g)+sum(h)+sum(i) &
           +sum(j)+sum(k)+sum(l)+sum(m)+sum(n)+sum(o)+sum(p)+sum(q)
  deallocate(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q)
end subroutine

subroutine allocate_pointer_controls(output)
  integer, intent(out) :: output
  integer, allocatable :: value
  interface
    function borrowed_scalar() result(target)
      integer, pointer :: target
    end function
    function borrowed_message() result(target)
      character(len=40), pointer :: target
    end function
  end interface
  allocate(value, source=borrowed_scalar(), stat=borrowed_scalar(), errmsg=borrowed_message())
  output = value
  deallocate(value, stat=borrowed_scalar(), errmsg=borrowed_message())
  deallocate(value, stat=borrowed_scalar(), errmsg=borrowed_message())
end subroutine

subroutine fetch_empty_array(output)
  integer,intent(out) :: output
  interface
    function borrowed_array() result(value)
      integer,pointer :: value(:)
    end function
  end interface
  output = sum(borrowed_array())
end subroutine
subroutine fetch_owned(output)
  integer, intent(out) :: output
  interface
    function owned_scalar() result(value)
      integer, allocatable :: value
    end function
  end interface
  output = owned_scalar()
end subroutine
subroutine fetch_array(output)
  integer, intent(out) :: output(3)
  interface
    function borrowed_array() result(value)
      integer, pointer :: value(:)
    end function
  end interface
  output = borrowed_array()
end subroutine
subroutine fetch_character(output)
  character(len=*), intent(out) :: output
  interface
    function owned_character() result(value)
      character(len=:), allocatable :: value
    end function
  end interface
  output = owned_character()
end subroutine

subroutine fetch_owned_constructor(output)
  integer, intent(out) :: output(17)
  integer :: i
  interface
    function owned_scalar() result(value)
      integer, allocatable :: value
    end function
  end interface
  output = [(owned_scalar(), i=1,17)]
end subroutine
subroutine fetch_character_constructor(output)
  character(len=*), intent(out) :: output(3)
  integer :: i
  interface
    function owned_character() result(value)
      character(len=:), allocatable :: value
    end function
  end interface
  output = [(owned_character(), i=1,3)]
end subroutine
subroutine fetch_nested_constructor(output)
  integer, intent(out) :: output(2)
  integer :: i
  interface
    function owned_scalar() result(value)
      integer, allocatable :: value
    end function
  end interface
  output = [sum([(owned_scalar(), i=1,17)]), owned_scalar()]
end subroutine
