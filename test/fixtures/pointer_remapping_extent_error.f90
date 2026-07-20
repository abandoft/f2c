program pointer_remapping_extent_error
  implicit none
  integer, target :: storage(5)
  integer, pointer :: matrix(:, :)
  integer :: columns

  columns = 3
  matrix(1:2, 1:columns) => storage
end program pointer_remapping_extent_error
