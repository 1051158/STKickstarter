import { ReactNode, useEffect, useState } from 'react';
import { ensureUserId, Room, supabase } from './lib/supabase';
import ConnectDeviceScreen from './screens/ConnectDeviceScreen';
import GroupsScreen from './screens/GroupsScreen';
import RoomScreen from './screens/RoomScreen';
import WelcomeScreen from './screens/WelcomeScreen';
import { Body, BottomNav, ErrorText, Screen, Tab } from './ui';

export default function App() {
  const [userId, setUserId] = useState<string | null>(null);
  const [tab, setTab] = useState<Tab>('home');
  const [room, setRoom] = useState<Room | null>(null);
  const [displayName, setDisplayName] = useState<string | null>(null);
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
      if (data[0]) setRoom(data[0]);
      setUserId(id);
    })().catch((e: Error) => setError(e.message));
  }, []);

  if (!userId) {
    return (
      <Screen centered>
        {error ? <ErrorText message={error} /> : <Body>Loading...</Body>}
      </Screen>
    );
  }

  let content: ReactNode;
  if (tab === 'connect') content = <ConnectDeviceScreen />;
  else if (room) content = <RoomScreen room={room} userId={userId} onLeft={() => setRoom(null)} />;
  else if (displayName) content = <GroupsScreen displayName={displayName} onOpened={setRoom} />;
  else {
    return (
      <Screen centered>
        <WelcomeScreen onSubmit={setDisplayName} />
      </Screen>
    );
  }

  return <Screen nav={<BottomNav active={tab} onChange={setTab} />}>{content}</Screen>;
}
