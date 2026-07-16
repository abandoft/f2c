program transform_intrinsics
  implicit none
  integer :: left(2, 3), right(3, 2), product_matrix(2, 2), transposed(3, 2)
  integer :: source(6), shape2(2), order2(2), pad2(2), small(2)
  integer :: reshaped(2, 3), reordered(2, 3), padded(2, 2)
  integer :: packed(3), packed_vector(4), vector4(4)
  integer :: unpacked(5), unpack_vector(3), unpack_field(5)
  integer :: spread_source(3), spread_dim1(2, 3), spread_dim2(3, 2), dim
  integer :: shifted(2, 3), shifted_array(2, 3), ended(2, 3), ended_array(2, 3)
  integer :: shifts(3), boundaries(2), locations(2), dim_locations_1(3), dim_locations_2(2)
  logical :: pack_mask(6), unpack_mask(5), location_mask(2, 3)

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

  source = (/1, 2, 3, 4, 5, 6/)
  shape2 = (/2, 3/)
  order2 = (/2, 1/)
  reshaped = reshape(source, shape2)
  reordered = reshape(source, shape2, order=order2)
  small = (/1, 2/)
  pad2 = (/9, 8/)
  padded = reshape(small, (/2, 2/), pad=pad2)
  if (any(reshaped /= left)) stop 5
  if (reordered(1, 1) /= 1 .or. reordered(2, 1) /= 4 .or. &
      reordered(1, 2) /= 2 .or. reordered(2, 2) /= 5 .or. &
      reordered(1, 3) /= 3 .or. reordered(2, 3) /= 6) stop 6
  if (padded(1, 1) /= 1 .or. padded(2, 1) /= 2 .or. &
      padded(1, 2) /= 9 .or. padded(2, 2) /= 8) stop 7

  pack_mask = (/.true., .false., .true., .false., .true., .false./)
  packed = pack(source, pack_mask)
  vector4 = (/7, 8, 9, 10/)
  packed_vector = pack(array=source, mask=pack_mask, vector=vector4)
  if (any(packed /= (/1, 3, 5/))) stop 8
  if (any(packed_vector /= (/1, 3, 5, 10/))) stop 9

  unpack_vector = (/10, 20, 30/)
  unpack_mask = (/.true., .false., .true., .false., .true./)
  unpack_field = (/1, 2, 3, 4, 5/)
  unpacked = unpack(unpack_vector, unpack_mask, unpack_field)
  if (any(unpacked /= (/10, 2, 20, 4, 30/))) stop 10

  spread_source = (/1, 2, 3/)
  dim = 1
  spread_dim1 = spread(spread_source, dim, 2)
  dim = 2
  spread_dim2 = spread(source=spread_source, dim=dim, ncopies=2)
  if (spread_dim1(1, 1) /= 1 .or. spread_dim1(2, 1) /= 1 .or. &
      spread_dim1(1, 2) /= 2 .or. spread_dim1(2, 2) /= 2 .or. &
      spread_dim1(1, 3) /= 3 .or. spread_dim1(2, 3) /= 3) stop 11
  if (spread_dim2(1, 1) /= 1 .or. spread_dim2(2, 1) /= 2 .or. &
      spread_dim2(3, 1) /= 3 .or. spread_dim2(1, 2) /= 1 .or. &
      spread_dim2(2, 2) /= 2 .or. spread_dim2(3, 2) /= 3) stop 12

  shifted = cshift(left, 1, dim=1)
  shifts = (/0, 1, -1/)
  shifted_array = cshift(left, shifts, dim=1)
  ended = eoshift(left, 1, boundary=-1, dim=2)
  boundaries = (/90, 80/)
  ended_array = eoshift(array=left, shift=1, boundary=boundaries, dim=2)
  if (any(shifted /= reshape((/2, 1, 4, 3, 6, 5/), (/2, 3/)))) stop 13
  if (any(shifted_array /= reshape((/1, 2, 4, 3, 6, 5/), (/2, 3/)))) stop 14
  if (any(ended /= reshape((/3, 4, 5, 6, -1, -1/), (/2, 3/)))) stop 15
  if (any(ended_array /= reshape((/3, 4, 5, 6, 90, 80/), (/2, 3/)))) stop 16

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
end program transform_intrinsics
