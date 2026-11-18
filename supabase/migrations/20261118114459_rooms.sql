-- Rooms: a closed group of friends. Only members can see a room or its members.
-- Creating and joining go through create_room / join_room, so clients never read PINs of rooms they're not in.

create table public.rooms (
  id uuid primary key default gen_random_uuid(),
  name text not null check (char_length(btrim(name)) between 1 and 40),
  pin text not null check (pin ~ '^[0-9]{6}$'),
  created_by uuid not null references auth.users (id) on delete cascade,
  created_at timestamptz not null default now()
);

create unique index rooms_name_pin_key on public.rooms (lower(name), pin);

create table public.room_members (
  room_id uuid not null references public.rooms (id) on delete cascade,
  user_id uuid not null references auth.users (id) on delete cascade,
  display_name text not null check (char_length(btrim(display_name)) between 1 and 30),
  joined_at timestamptz not null default now(),
  primary key (room_id, user_id)
);

create index room_members_user_id_idx on public.room_members (user_id);

alter table public.rooms enable row level security;
alter table public.room_members enable row level security;

-- Helpers live in a schema the Data API doesn't expose.
create schema private;
grant usage on schema private to authenticated;

create function private.is_room_member(p_room_id uuid)
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
  select exists (
    select 1 from public.room_members
    where room_id = p_room_id and user_id = (select auth.uid())
  );
$$;

revoke execute on function private.is_room_member(uuid) from public, anon;
grant execute on function private.is_room_member(uuid) to authenticated;

create policy "Members can see their rooms" on public.rooms
  for select to authenticated
  using (private.is_room_member(id));

create policy "Members can see each other" on public.room_members
  for select to authenticated
  using (private.is_room_member(room_id));

create policy "Members can leave a room" on public.room_members
  for delete to authenticated
  using (user_id = (select auth.uid()));

-- Failed joins, to slow down PIN guessing. Counted per user and per room name,
-- because a guesser can make new anonymous users but not new names for the room they target.
create table private.join_attempts (
  user_id uuid not null references auth.users (id) on delete cascade,
  room_name text not null,
  attempted_at timestamptz not null default now()
);

create index join_attempts_user_id_idx on private.join_attempts (user_id, attempted_at);
create index join_attempts_room_name_idx on private.join_attempts (room_name, attempted_at);

create function public.create_room(p_name text, p_display_name text)
returns public.rooms
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_room public.rooms;
begin
  if (select auth.uid()) is null then
    raise exception 'Not signed in';
  end if;

  loop
    begin
      insert into public.rooms (name, pin, created_by)
      values (btrim(p_name), lpad(((('x' || encode(extensions.gen_random_bytes(4), 'hex'))::bit(32)::bigint) % 1000000)::text, 6, '0'), (select auth.uid()))
      returning * into v_room;
      exit;
    exception when unique_violation then
      -- Same name and PIN already taken: roll a new PIN.
    end;
  end loop;

  insert into public.room_members (room_id, user_id, display_name)
  values (v_room.id, (select auth.uid()), btrim(p_display_name));

  return v_room;
end;
$$;

create function public.join_room(p_name text, p_pin text, p_display_name text)
returns public.rooms
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_room public.rooms;
begin
  if (select auth.uid()) is null then
    raise exception 'Not signed in';
  end if;

  -- One join per room name at a time, so parallel guesses can't all pass the count below.
  perform pg_advisory_xact_lock(hashtext(lower(btrim(p_name))));

  if (
    select count(*) from private.join_attempts
    where user_id = (select auth.uid()) and attempted_at > now() - interval '10 minutes'
  ) >= 10
  or (
    select count(*) from private.join_attempts
    where room_name = lower(btrim(p_name)) and attempted_at > now() - interval '10 minutes'
  ) >= 30 then
    raise exception 'Too many attempts. Try again in a few minutes.';
  end if;

  select * into v_room from public.rooms
  where lower(name) = lower(btrim(p_name)) and pin = btrim(p_pin);

  -- Return null instead of raising: an exception would roll back the attempt we just logged.
  if v_room.id is null then
    insert into private.join_attempts (user_id, room_name) values ((select auth.uid()), lower(btrim(p_name)));
    return null;
  end if;

  insert into public.room_members (room_id, user_id, display_name)
  values (v_room.id, (select auth.uid()), btrim(p_display_name))
  on conflict (room_id, user_id) do update set display_name = excluded.display_name;

  return v_room;
end;
$$;

revoke execute on function public.create_room(text, text) from public, anon;
revoke execute on function public.join_room(text, text, text) from public, anon;
grant execute on function public.create_room(text, text) to authenticated;
grant execute on function public.join_room(text, text, text) to authenticated;

-- Live member list: a private broadcast on topic room:<id> that only members can receive.
-- The payload is empty; clients refetch the member list, which RLS already guards.
create function private.broadcast_members_changed()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
begin
  perform realtime.send('{}'::jsonb, 'members_changed', 'room:' || coalesce(new.room_id, old.room_id)::text, true);
  return null;
end;
$$;

create trigger room_members_broadcast
  after insert or update or delete on public.room_members
  for each row execute function private.broadcast_members_changed();

create function private.room_id_from_topic(p_topic text)
returns uuid
language sql
immutable
set search_path = ''
as $$
  select case
    when p_topic ~ '^room:[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$'
    then substring(p_topic from 6)::uuid
  end;
$$;

create policy "Members receive their room's broadcasts" on realtime.messages
  for select to authenticated
  using (
    realtime.messages.extension = 'broadcast'
    and private.is_room_member(private.room_id_from_topic((select realtime.topic())))
  );
