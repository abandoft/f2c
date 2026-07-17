program transform_intrinsics
  implicit none
  integer :: left(2, 3), right(3, 2), product_matrix(2, 2), transposed(3, 2)
  integer :: expression_product(2, 2), expression_transposed(3, 2)
  integer :: nested_transposed(2, 3)
  integer :: matrix_vector(2), vector_matrix(3), vector3(3), vector2(2)
  integer, allocatable :: dynamic_matrix(:, :), dynamic_vector(:)
  integer, allocatable :: zero_left(:, :), zero_right(:, :), zero_product(:, :)
  integer :: source(6), shape2(2), order2(2), pad2(2), small(2)
  integer :: vector_indices(3), vector_shifted(3)
  integer :: reshaped(2, 3), reordered(2, 3), padded(2, 2)
  integer :: expression_reshaped(2, 3), section_reshaped(2, 3)
  integer :: packed(3), packed_vector(4), vector4(4)
  integer :: expression_packed(3), expression_mask_packed(3)
  integer :: unpacked(5), unpack_vector(3), unpack_field(5)
  integer :: expression_unpacked(5)
  integer :: spread_source(3), spread_dim1(2, 3), spread_dim2(3, 2), dim
  integer :: expression_spread(3, 2)
  integer :: shifted(2, 3), shifted_array(2, 3), ended(2, 3), ended_array(2, 3)
  integer :: expression_shifted(2, 3)
  integer :: shifts(3), boundaries(2), locations(2), dim_locations_1(3), dim_locations_2(2)
  integer :: expression_locations(2), masked_expression_locations(2)
  logical :: pack_mask(6), unpack_mask(5), location_mask(2, 3)
  logical :: logical_left(2, 2), logical_right(2, 2), logical_product(2, 2)
  complex :: complex_left(1, 2), complex_right(2, 1), complex_product(1, 1)
  integer(kind=8) :: wide_left(1, 2), wide_product(1, 1)
  integer :: narrow_right(2, 1)
  real :: real_left(1, 2)
  double precision :: double_right(2, 1), mixed_real_product(1, 1)
  complex(kind=8) :: mixed_complex_product(1, 1)
  double precision :: double_scalar_matrix(1, 1)

  left(1, 1) = 1
  left(2, 1) = 2
  left(1, 2) = 3
  left(2, 2) = 4
  left(1, 3) = 5
  left(2, 3) = 6
  right(1, 1) = 7
  right(2, 1) = 8
  right(3, 1) = 9
  right(1, 2) = 10
  right(2, 2) = 11
  right(3, 2) = 12
  transposed = transpose(left)
  product_matrix = matmul(left, right)

  if (transposed(1, 1) /= 1 .or. transposed(2, 1) /= 3 .or. &
      transposed(3, 1) /= 5) stop 1
  if (transposed(1, 2) /= 2 .or. transposed(2, 2) /= 4 .or. &
      transposed(3, 2) /= 6) stop 2
  if (product_matrix(1, 1) /= 76 .or. product_matrix(2, 1) /= 100) stop 3
  if (product_matrix(1, 2) /= 103 .or. product_matrix(2, 2) /= 136) stop 4
  expression_transposed = transpose(left + 10)
  expression_product = matmul(left + 1, right - 1)
  nested_transposed = transpose(transpose(left + 10))
  if (expression_transposed(1, 1) /= 11 .or. expression_transposed(2, 1) /= 13 .or. &
      expression_transposed(3, 1) /= 15 .or. expression_transposed(1, 2) /= 12 .or. &
      expression_transposed(2, 2) /= 14 .or. expression_transposed(3, 2) /= 16) stop 31
  if (expression_product(1, 1) /= 88 .or. expression_product(2, 1) /= 109 .or. &
      expression_product(1, 2) /= 124 .or. expression_product(2, 2) /= 154) stop 32
  if (nested_transposed(1, 1) /= 11 .or. nested_transposed(2, 1) /= 12 .or. &
      nested_transposed(1, 2) /= 13 .or. nested_transposed(2, 2) /= 14 .or. &
      nested_transposed(1, 3) /= 15 .or. nested_transposed(2, 3) /= 16) stop 47
  dynamic_matrix = transpose(left + 20)
  if (size(dynamic_matrix, 1) /= 3 .or. size(dynamic_matrix, 2) /= 2 .or. &
      dynamic_matrix(3, 2) /= 26) stop 39
  dynamic_matrix = matmul(left, right)
  if (size(dynamic_matrix, 1) /= 2 .or. size(dynamic_matrix, 2) /= 2) stop 40
  if (any(dynamic_matrix /= product_matrix)) stop 40
  vector3 = (/1, 2, 3/)
  vector2 = (/2, 3/)
  matrix_vector = matmul(left + 0, vector3 + 0)
  vector_matrix = matmul(vector2 + 0, left + 0)
  if (any(matrix_vector /= (/22, 28/))) stop 33
  if (any(vector_matrix /= (/8, 18, 28/))) stop 34
  dynamic_vector = matmul(left + 0, vector3 + 0)
  if (size(dynamic_vector) /= 2) stop 41
  if (any(dynamic_vector /= matrix_vector)) stop 41
  allocate(zero_left(2, 0), zero_right(0, 3))
  zero_product = matmul(zero_left, zero_right)
  if (size(zero_product, 1) /= 2 .or. size(zero_product, 2) /= 3) stop 42
  if (any(zero_product /= 0)) stop 42
  dynamic_matrix = transpose(zero_left)
  if (size(dynamic_matrix, 1) /= 0 .or. size(dynamic_matrix, 2) /= 2) stop 43
  logical_left = reshape((/.true., .false., .false., .true./), (/2, 2/))
  logical_right = reshape((/.true., .true., .false., .true./), (/2, 2/))
  logical_product = matmul(logical_left, logical_right)
  if (any(logical_product .neqv. logical_right)) stop 35
  complex_left(1, 1) = (1.0, 1.0)
  complex_left(1, 2) = (2.0, -1.0)
  complex_right(1, 1) = (1.0, 0.0)
  complex_right(2, 1) = (0.0, 1.0)
  complex_product = matmul(complex_left + (0.0, 0.0), complex_right + (0.0, 0.0))
  if (abs(real(complex_product(1, 1)) - 2.0) > 1.0e-6 .or. &
      abs(aimag(complex_product(1, 1)) - 3.0) > 1.0e-6) stop 36
  wide_left = reshape((/3000000000_8, 4000000000_8/), (/1, 2/))
  narrow_right = reshape((/2, 3/), (/2, 1/))
  wide_product = matmul(wide_left, narrow_right)
  if (wide_product(1, 1) /= 18000000000_8) stop 44
  real_left = reshape((/1.5, 2.5/), (/1, 2/))
  double_right = reshape((/2.0d0, 4.0d0/), (/2, 1/))
  mixed_real_product = matmul(real_left, double_right)
  if (abs(mixed_real_product(1, 1) - 13.0d0) > 1.0d-12) stop 45
  double_scalar_matrix(1, 1) = 2.0d0
  mixed_complex_product = matmul(complex_left(:, 1:1), double_scalar_matrix)
  if (abs(real(mixed_complex_product(1, 1)) - 2.0d0) > 1.0d-12 .or. &
      abs(aimag(mixed_complex_product(1, 1)) - 2.0d0) > 1.0d-12) stop 46

  source = (/1, 2, 3, 4, 5, 6/)
  vector_indices = (/6, 2, 4/)
  vector_shifted = cshift(source(vector_indices), 1)
  if (any(vector_shifted /= (/2, 4, 6/))) stop 49
  shape2 = shape(left)
  order2 = (/2, 1/)
  reshaped = reshape(source, shape(left))
  reordered = reshape(source, shape(left), order=order2 + 0)
  small = (/1, 2/)
  pad2 = (/9, 8/)
  padded = reshape(small, shape(padded), pad=pad2 + 0)
  if (any(reshaped /= left)) stop 5
  if (reordered(1, 1) /= 1 .or. reordered(2, 1) /= 4 .or. &
      reordered(1, 2) /= 2 .or. reordered(2, 2) /= 5 .or. &
      reordered(1, 3) /= 3 .or. reordered(2, 3) /= 6) stop 6
  if (padded(1, 1) /= 1 .or. padded(2, 1) /= 2 .or. &
      padded(1, 2) /= 9 .or. padded(2, 2) /= 8) stop 7
  expression_reshaped = reshape(source + 10, shape2 + 0)
  section_reshaped = reshape(source(6:1:-1), shape(left))
  if (expression_reshaped(1, 1) /= 11 .or. expression_reshaped(2, 1) /= 12 .or. &
      expression_reshaped(1, 2) /= 13 .or. expression_reshaped(2, 2) /= 14 .or. &
      expression_reshaped(1, 3) /= 15 .or. expression_reshaped(2, 3) /= 16) stop 22
  if (any(section_reshaped /= reshape((/6, 5, 4, 3, 2, 1/), shape(left)))) stop 23

  pack_mask = (/.true., .false., .true., .false., .true., .false./)
  packed = pack(source, pack_mask)
  vector4 = (/7, 8, 9, 10/)
  packed_vector = pack(array=source, mask=pack_mask, vector=vector4)
  if (any(packed /= (/1, 3, 5/))) stop 8
  if (any(packed_vector /= (/1, 3, 5, 10/))) stop 9
  expression_packed = pack(source * 2, pack_mask)
  if (any(expression_packed /= (/2, 6, 10/))) stop 24
  expression_mask_packed = pack(source + 10, source >= 3 .and. source <= 5)
  if (any(expression_mask_packed /= (/13, 14, 15/))) stop 29

  unpack_vector = (/10, 20, 30/)
  unpack_mask = (/.true., .false., .true., .false., .true./)
  unpack_field = (/1, 2, 3, 4, 5/)
  unpacked = unpack(unpack_vector, unpack_mask, unpack_field)
  if (any(unpacked /= (/10, 2, 20, 4, 30/))) stop 10
  expression_unpacked = unpack(unpack_vector + 1, &
      unpack_field == 1 .or. unpack_field == 3 .or. unpack_field == 5, unpack_field + 10)
  if (any(expression_unpacked /= (/11, 12, 21, 14, 31/))) stop 25

  spread_source = (/1, 2, 3/)
  dim = 1
  spread_dim1 = spread(spread_source, dim, size(left, 1))
  dim = 2
  spread_dim2 = spread(source=spread_source, dim=dim, ncopies=size(right, 2))
  if (spread_dim1(1, 1) /= 1 .or. spread_dim1(2, 1) /= 1 .or. &
      spread_dim1(1, 2) /= 2 .or. spread_dim1(2, 2) /= 2 .or. &
      spread_dim1(1, 3) /= 3 .or. spread_dim1(2, 3) /= 3) stop 11
  if (spread_dim2(1, 1) /= 1 .or. spread_dim2(2, 1) /= 2 .or. &
      spread_dim2(3, 1) /= 3 .or. spread_dim2(1, 2) /= 1 .or. &
      spread_dim2(2, 2) /= 2 .or. spread_dim2(3, 2) /= 3) stop 12
  expression_spread = spread(spread_source + 3, dim=2, ncopies=2)
  if (any(expression_spread /= reshape((/4, 5, 6, 4, 5, 6/), (/3, 2/)))) stop 26

  shifted = cshift(left, 1, dim=1)
  shifts = (/0, 1, -1/)
  shifted_array = cshift(left, shifts + 0, dim=1)
  ended = eoshift(left, 1, boundary=-1, dim=2)
  boundaries = (/90, 80/)
  ended_array = eoshift(array=left, shift=1, boundary=boundaries + 0, dim=2)
  if (any(shifted /= reshape((/2, 1, 4, 3, 6, 5/), (/2, 3/)))) stop 13
  if (any(shifted_array /= reshape((/1, 2, 4, 3, 6, 5/), (/2, 3/)))) stop 14
  if (any(ended /= reshape((/3, 4, 5, 6, -1, -1/), (/2, 3/)))) stop 15
  if (any(ended_array /= reshape((/3, 4, 5, 6, 90, 80/), (/2, 3/)))) stop 16
  expression_shifted = cshift(left + 10, 1, dim=1)
  if (any(expression_shifted /= reshape((/12, 11, 14, 13, 16, 15/), (/2, 3/)))) stop 27

  locations = findloc(left, 2)
  if (any(locations /= (/2, 1/))) stop 17
  locations = findloc(left, 2, back=.true.)
  if (any(locations /= (/2, 1/))) stop 18
  location_mask = .false.
  location_mask(2, 2) = .true.
  locations = findloc(array=left, value=4, mask=location_mask)
  if (any(locations /= (/2, 2/))) stop 19
  dim_locations_1 = findloc(left, 2, dim=1)
  dim_locations_2 = findloc(left, 2, dim=2)
  if (any(dim_locations_1 /= (/2, 0, 0/))) stop 20
  if (any(dim_locations_2 /= (/0, 1/))) stop 21
  expression_locations = findloc(left + 10, 12)
  if (any(expression_locations /= (/2, 1/))) stop 28
  masked_expression_locations = findloc(left + 10, 14, mask=left > 3)
  if (any(masked_expression_locations /= (/2, 2/))) stop 30

  call check_assumed(left(2:1:-1, 3:1:-1), right(3:1:-1, 2:1:-1))

contains

  subroutine check_assumed(assumed_left, assumed_right)
    integer, intent(in) :: assumed_left(:, :), assumed_right(:, :)
    integer :: assumed_product(2, 2), assumed_transposed(3, 2)

    assumed_transposed = transpose(assumed_left)
    assumed_product = matmul(assumed_left, assumed_right)
    if (any(assumed_transposed /= reshape((/6, 4, 2, 5, 3, 1/), (/3, 2/)))) stop 37
    if (any(assumed_product /= reshape((/136, 103, 100, 76/), (/2, 2/)))) stop 38
  end subroutine check_assumed
end program transform_intrinsics
