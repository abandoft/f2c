program contiguous_pointer_error
  implicit none
  integer, target :: storage(8)
  integer, pointer, contiguous :: view(:)

  view => storage(1:8:2)
end program contiguous_pointer_error
