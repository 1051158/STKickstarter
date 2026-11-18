import { useEffect, useState } from 'react';
import { StyleSheet, Text } from 'react-native';
import { Room, supabase } from '../lib/supabase';
import { colors, font } from '../theme';
import { Body, Button, ErrorText, Screen, Title } from '../ui';

type Member = { user_id: string; display_name: string; joined_at: string };
type Props = { room: Room; userId: string; onLeft: () => void };

export default function RoomScreen({ room, userId, onLeft }: Props) {
  const [members, setMembers] = useState<Member[]>([]);
  const [leaving, setLeaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    async function load() {
      const { data, error } = await supabase
        .from('room_members')
        .select('user_id, display_name, joined_at')
        .eq('room_id', room.id)
        .order('joined_at');
      if (error) setError(error.message);
      else setMembers(data);
    }
    load();
    const channel = supabase
      .channel(`room_members:${room.id}`)
      .on(
        'postgres_changes',
        { event: '*', schema: 'public', table: 'room_members', filter: `room_id=eq.${room.id}` },
        load,
      )
      .subscribe();
    return () => {
      supabase.removeChannel(channel);
    };
  }, [room.id]);

  async function leave() {
    setLeaving(true);
    setError(null);
    const { error } = await supabase
      .from('room_members')
      .delete()
      .eq('room_id', room.id)
      .eq('user_id', userId);
    setLeaving(false);
    if (error) return setError(error.message);
    onLeft();
  }

  return (
    <Screen>
      <Title>{room.name}</Title>
      <Body>Share this name and PIN with your friends</Body>
      <Text style={styles.pin} selectable>
        {room.pin}
      </Text>
      <Body>Members</Body>
      {members.map((m) => (
        <Body key={m.user_id}>
          {m.display_name}
          {m.user_id === userId ? ' (you)' : ''}
        </Body>
      ))}
      <Button label="Leave room" onPress={leave} disabled={leaving} />
      <ErrorText message={error} />
    </Screen>
  );
}

const styles = StyleSheet.create({
  pin: { fontSize: font.pin, fontWeight: 'bold', fontVariant: ['tabular-nums'], color: colors.text },
});
