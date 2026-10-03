program qualified_descriptors
  character(len=3), volatile :: words(3)
  call consume(words(3:1:-1))
contains
  subroutine consume(values)
    character(len=*), volatile :: values(:)
    values = 'new'
    if (count(values == 'new') /= 3) stop 1
  end subroutine consume
end program qualified_descriptors
