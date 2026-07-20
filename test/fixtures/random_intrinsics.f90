program random_intrinsics
  implicit none
  integer :: seed_size, index, row, column
  integer :: seed(64), saved(64)
  real :: first, second, replay, reset_first, reset_second
  real :: values(2,3)
  logical :: in_range, replayed, reset_valid, section_preserved

  call random_seed(size=seed_size)
  do index = 1, seed_size
    seed(index) = 7919 * index + 17
  end do

  call random_seed(put=seed)
  call random_number(first)
  call random_seed(get=saved)
  call random_number(second)
  call random_seed(put=saved)
  call random_number(replay)

  values = -1.0
  call random_number(values(:,2:3))
  in_range = first >= 0.0 .and. first < 1.0 .and. second >= 0.0 .and. second < 1.0
  do column = 2, 3
    do row = 1, 2
      in_range = in_range .and. values(row,column) >= 0.0 .and. values(row,column) < 1.0
    end do
  end do
  replayed = second == replay
  section_preserved = values(1,1) == -1.0 .and. values(2,1) == -1.0

  call random_seed()
  call random_number(reset_first)
  call random_seed()
  call random_number(reset_second)
  reset_valid = reset_first >= 0.0 .and. reset_first < 1.0 .and. &
                reset_second >= 0.0 .and. reset_second < 1.0

  write (*, '(4(L1,1X))') in_range, replayed, reset_valid, section_preserved
end program random_intrinsics
