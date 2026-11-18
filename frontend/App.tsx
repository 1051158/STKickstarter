import { useEffect, useState } from 'react';
import { ensureUserId, Room, supabase } from './lib/supabase';
import ConnectDeviceScreen from './screens/ConnectDeviceScreen';
import CreateRoomScreen from './screens/CreateRoomScreen';
import HomeScreen from './screens/HomeScreen';
import JoinRoomScreen from './screens/JoinRoomScreen';
import RoomScreen from './screens/RoomScreen';
import { Body, ErrorText, Screen } from './ui';

type Route = 'home' | 'create' | 'join' | 'connect' | 'room';

export default function App() {
  const [userId, setUserId] = useState<string | null>(null);
  const [route, setRoute] = useState<Route>('home');
  const [room, setRoom] = useState<Room | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    (async () => {
      const id = await ensureUserId();
      const { data, error } = await supabase
        .from('rooms')
        .select()
        .order('created_at', { ascending: false })
        .limit(1);
      if (error) throw error;
      if (data[0]) openRoom(data[0]);
      setUserId(id);
    })().catch((e: Error) => setError(e.message));
  }, []);

  function openRoom(r: Room) {
    setRoom(r);
    setRoute('room');
  }

  const home = () => setRoute('home');

  if (!userId) {
    return (
      <Screen>
        {error ? <ErrorText message={error} /> : <Body>Loading...</Body>}
      </Screen>
    );
  }

  switch (route) {
    case 'create':
      return <CreateRoomScreen onBack={home} onCreated={openRoom} />;
    case 'join':
      return <JoinRoomScreen onBack={home} onJoined={openRoom} />;
    case 'connect':
      return <ConnectDeviceScreen onBack={home} />;
    case 'room':
      if (room) return <RoomScreen room={room} userId={userId} onLeft={home} />;
  }
  return (
    <HomeScreen
      onCreate={() => setRoute('create')}
      onJoin={() => setRoute('join')}
      onConnect={() => setRoute('connect')}
    />
  );
}
