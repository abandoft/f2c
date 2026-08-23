program namelist_transaction
  implicit none

  type :: leaf
    integer :: id
    integer, allocatable :: values(:)
  end type leaf

  type :: container
    integer :: code
    type(leaf), allocatable :: items(:)
    type(leaf), pointer :: link
    character(len=:), allocatable :: label
  end type container

  type(container) :: state
  type(leaf), target :: linked
  integer :: numbers(2)
  integer, allocatable :: dynamic(:)
  integer, allocatable :: vacant(:)
  complex :: phase
  integer :: status
  character(1024) :: record
  namelist /snapshot/ state, numbers, dynamic, vacant, phase

  allocate(state%items(1))
  allocate(state%items(1)%values(2))
  allocate(linked%values(2))
  allocate(character(len=4) :: state%label)
  allocate(dynamic(2))
  state%link => linked

  state%code = 10
  state%items(1)%id = 20
  state%items(1)%values = [21, 22]
  linked%id = 30
  linked%values = [31, 32]
  state%label = 'safe'
  numbers = [40, 41]
  dynamic = [50, 51]
  phase = cmplx(60.0, 61.0)

  record = '&snapshot state%code=110, state%items(1)%id=120, ' // &
           'state%items(1)%values(1)=121, state%link%id=130, ' // &
           "state%label='risk', dynamic(1)=150, phase=(160.0,invalid), " // &
           'numbers=140,invalid /'
  read(record, nml=snapshot, iostat=status)

  if (status == 0) stop 1
  if (state%code /= 10 .or. state%items(1)%id /= 20) stop 2
  if (any(state%items(1)%values /= [21, 22])) stop 3
  if (linked%id /= 30 .or. any(linked%values /= [31, 32])) stop 4
  if (state%label /= 'safe') stop 5
  if (any(numbers /= [40, 41]) .or. any(dynamic /= [50, 51])) stop 6
  if (.not. associated(state%link, linked)) stop 7
  if (phase /= cmplx(60.0, 61.0)) stop 18

  record = '&snapshot state%code=111, stranger=7 /'
  read(record, nml=snapshot, iostat=status)
  if (status == 0 .or. state%code /= 10) stop 15

  record = '&snapshot state%code=111, state%missing=7 /'
  read(record, nml=snapshot, iostat=status)
  if (status == 0 .or. state%code /= 10) stop 21

  record = '&snapshot state%code=111, dynamic(999)=7 /'
  read(record, nml=snapshot, iostat=status)
  if (status == 0 .or. state%code /= 10 .or. any(dynamic /= [50, 51])) stop 22

  record = '&snapshot state%code=112'
  read(record, nml=snapshot, iostat=status)
  if (status == 0 .or. state%code /= 10) stop 16

  record = '&different state%code=113 /'
  read(record, nml=snapshot, iostat=status)
  if (status == 0 .or. state%code /= 10) stop 17

  record = '&snapshot state%code=114, vacant(2147483648)=1 /'
  read(record, nml=snapshot, iostat=status)
  if (status == 0 .or. state%code /= 10 .or. allocated(vacant)) stop 20

  record = '&snapshot state%code=115, numbers(2:1:0)=1,2 /'
  read(record, nml=snapshot, iostat=status)
  if (status == 0 .or. state%code /= 10 .or. any(numbers /= [40, 41])) stop 23

  record = "&snapshot state%code=116, state%label(2:99)='broken' /"
  read(record, nml=snapshot, iostat=status)
  if (status == 0 .or. state%code /= 10 .or. state%label /= 'safe') stop 24

  record = '&snapshot state%code=117, numbers=1,2,3 /'
  read(record, nml=snapshot, iostat=status)
  if (status == 0 .or. state%code /= 10 .or. any(numbers /= [40, 41])) stop 25

  record = '&snapshot state%code=210, state%items(1)%id=220, ' // &
           'state%items(1)%values(1)=221, state%items(1)%values(2)=222, ' // &
           'state%link%id=230, state%link%values(1)=231, ' // &
           "state%link%values(2)=232, state%label='done', " // &
           'numbers=240,241, dynamic(1)=250, dynamic(2)=251, phase=(260.0,261.0) /'
  read(record, nml=snapshot, iostat=status)

  if (status /= 0) stop 8
  if (state%code /= 210 .or. state%items(1)%id /= 220) stop 9
  if (any(state%items(1)%values /= [221, 222])) stop 10
  if (linked%id /= 230 .or. any(linked%values /= [231, 232])) stop 11
  if (state%label /= 'done') stop 12
  if (any(numbers /= [240, 241]) .or. any(dynamic /= [250, 251])) stop 13
  if (.not. associated(state%link, linked)) stop 14
  if (phase /= cmplx(260.0, 261.0)) stop 19
end program namelist_transaction
