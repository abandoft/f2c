module scopes
  integer, volatile :: stored
contains
  subroutine ordinary(value)
    integer :: value
    value = value + 1
  end subroutine
  subroutine qualified(value)
    integer, volatile :: value
    call ordinary(value)
    value = value + stored
  end subroutine
end module
