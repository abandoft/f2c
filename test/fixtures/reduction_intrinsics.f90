program reduction_intrinsics
  implicit none
  integer :: values(5)
  logical :: flags(5)
  integer(kind=8) :: wide_values(2)
  real :: real_values(2)
  complex :: complex_values(2), complex_weights(2)
  complex(kind=8) :: wide_complex_values(2), wide_complex_weights(2)
  logical(kind=1) :: compact_flags(3)
  logical(kind=8) :: wide_flags(3)
  integer :: tied_values(4)
  logical(kind=1) :: selected(4)
  integer :: matrix(2, 3), sum_columns(3), sum_rows(2)
  integer :: max_columns(3), first_max_rows(3), last_max_rows(3)
  integer :: global_max_location(2)
  integer(kind=1) :: selected_counts(3)
  logical(kind=1) :: matrix_mask(2, 3)
  logical :: all_columns(3), any_rows(2)
  integer, allocatable :: dynamic_sum(:)
  integer :: selected_dimension
  integer :: cube(2, 2, 2), cube_sum(2, 2)
  integer :: reversed_columns(3), expression_columns(3), masked_out_columns(3)
  integer :: empty(0, 2), empty_sum(2), empty_product(2), empty_maximum(2), empty_minimum(2)
  logical :: empty_flags(0, 2), empty_all(2), empty_any(2)
  integer :: empty_count(2)
  complex :: complex_matrix(2, 2), complex_columns(2)

  values = [2, 3, 4, 5, 6]
  flags = [.true., .false., .true., .true., .false.]
  wide_values = [2_8, 3_8]
  real_values = [4.0, 5.0]
  complex_values = [(1.0, 2.0), (3.0, 4.0)]
  complex_weights = [(5.0, 6.0), (7.0, 8.0)]
  wide_complex_values = [(1.0_8, 2.0_8), (3.0_8, 4.0_8)]
  wide_complex_weights = [(5.0_8, 6.0_8), (7.0_8, 8.0_8)]
  compact_flags = [.true., .false., .true.]
  wide_flags = [.false., .true., .true.]
  tied_values = [2, 9, 9, 4]
  selected = [.false., .true., .true., .false.]
  matrix = reshape([1, 2, 3, 4, 5, 6], [2, 3])
  matrix_mask = reshape([.true., .false., .false., .true., .true., .true.], [2, 3])
  cube = reshape([1, 2, 3, 4, 5, 6, 7, 8], [2, 2, 2])
  complex_matrix = reshape([(1.0, 2.0), (3.0, 4.0), (5.0, 6.0), (7.0, 8.0)], [2, 2])

  if (sum(values) /= 20) stop 1
  if (sum(values(5:1:-2)) /= 12) stop 2
  if (product(values(2:4)) /= 60) stop 3
  if (maxval(values(1:5:2)) /= 6) stop 4
  if (minval(values) /= 2) stop 5
  if (maxloc(values, dim=1) /= 5) stop 6
  if (minloc(values, dim=1) /= 1) stop 7
  if (count(flags) /= 3) stop 8
  if (.not. any(flags)) stop 9
  if (all(flags)) stop 10
  if (dot_product(values, [1, 2, 3, 4, 5]) /= 70) stop 11
  if (dot_product(wide_values, real_values) /= 23.0) stop 12
  if (sum(complex_values) /= (4.0, 6.0)) stop 13
  if (product(complex_values) /= (-5.0, 10.0)) stop 14
  if (dot_product(complex_values, complex_weights) /= (70.0, -8.0)) stop 15
  if (dot_product(wide_complex_values, wide_complex_weights) /= (70.0_8, -8.0_8)) stop 16
  if (.not. dot_product(compact_flags, wide_flags)) stop 17
  if (sum(tied_values, mask=selected) /= 18) stop 18
  if (product(tied_values, mask=selected) /= 81) stop 19
  if (maxval(tied_values, mask=selected) /= 9) stop 20
  if (minval(tied_values, mask=selected) /= 9) stop 21
  if (sum(tied_values, mask=.false.) /= 0) stop 22
  if (product(tied_values, mask=.false.) /= 1) stop 23
  if (maxloc(tied_values, dim=1, mask=selected, kind=8) /= 2_8) stop 24
  if (maxloc(tied_values, dim=1, mask=selected, back=.true.) /= 3) stop 25
  if (minloc(tied_values, dim=1, mask=selected, back=.true.) /= 3) stop 26
  if (count(selected, kind=1) /= 2_1) stop 27
  if (sum(complex_values, mask=[.true., .false.]) /= (1.0, 2.0)) stop 28
  sum_columns = sum(matrix, dim=1)
  sum_rows = sum(matrix, dim=2)
  max_columns = maxval(matrix, dim=1, mask=matrix_mask)
  selected_counts = count(matrix_mask, dim=1, kind=1)
  all_columns = all(matrix_mask, dim=1)
  any_rows = any(matrix_mask, dim=2)
  first_max_rows = maxloc(matrix, dim=1)
  last_max_rows = maxloc(matrix, dim=1, back=.true.)
  global_max_location = maxloc(matrix)
  selected_dimension = 2
  dynamic_sum = sum(matrix, dim=selected_dimension)
  cube_sum = sum(cube, dim=2)
  reversed_columns = sum(matrix(:, 3:1:-1), dim=1)
  expression_columns = sum(matrix + 1, dim=1)
  masked_out_columns = sum(matrix, dim=1, mask=.false.)
  empty_sum = sum(empty, dim=1)
  empty_product = product(empty, dim=1)
  empty_maximum = maxval(empty, dim=1)
  empty_minimum = minval(empty, dim=1)
  empty_all = all(empty_flags, dim=1)
  empty_any = any(empty_flags, dim=1)
  empty_count = count(empty_flags, dim=1)
  complex_columns = sum(complex_matrix, dim=1)
  if (any(sum_columns /= [3, 7, 11])) stop 29
  if (any(sum_rows /= [9, 12])) stop 30
  if (any(max_columns /= [1, 4, 6])) stop 31
  if (selected_counts(1) /= 1_1 .or. selected_counts(2) /= 1_1 .or. &
      selected_counts(3) /= 2_1) stop 32
  if (all_columns(1) .or. all_columns(2) .or. .not. all_columns(3)) stop 33
  if (.not. any_rows(1) .or. .not. any_rows(2)) stop 34
  if (any(first_max_rows /= [2, 2, 2])) stop 35
  if (any(last_max_rows /= [2, 2, 2])) stop 36
  if (any(global_max_location /= [2, 3])) stop 37
  if (.not. allocated(dynamic_sum)) stop 38
  if (any(dynamic_sum /= [9, 12])) stop 39
  if (any(cube_sum /= reshape([4, 6, 12, 14], [2, 2]))) stop 40
  if (any(reversed_columns /= [11, 7, 3])) stop 41
  if (any(expression_columns /= [5, 9, 13])) stop 42
  if (any(masked_out_columns /= [0, 0, 0])) stop 43
  if (any(empty_sum /= [0, 0])) stop 44
  if (any(empty_product /= [1, 1])) stop 45
  if (empty_maximum(1) /= maxval(empty(:, 1)) .or. &
      empty_maximum(2) /= maxval(empty(:, 2))) stop 46
  if (any(empty_minimum /= [huge(0), huge(0)])) stop 47
  if (.not. all(empty_all) .or. any(empty_any) .or. any(empty_count /= [0, 0])) stop 48
  if (any(complex_columns /= [(4.0, 6.0), (12.0, 14.0)])) stop 49
  print '(A)', 'reduction intrinsic differential passed'
end program reduction_intrinsics
