subroutine fetch_scalar(output)
  integer, intent(out) :: output
  interface
    function borrowed_scalar() result(value)
      integer, pointer :: value
    end function
  end interface
  output = borrowed_scalar()
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
