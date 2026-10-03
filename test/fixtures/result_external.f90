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
