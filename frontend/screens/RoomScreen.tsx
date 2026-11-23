import * as Clipboard from 'expo-clipboard';
import { useEffect, useState } from 'react';
import { StyleSheet, Text } from 'react-native';
import { Room, supabase } from '../lib/supabase';
import { colors, font } from '../theme';
import { Body, Button, Card, Chip, ErrorText, Title } from '../ui';

type Member = { user_id: string; display_name: string; joined_at: string };
type Props = { room: Room; userId: string; onLeft: () => void };

export default function RoomScreen({ room, userId, onLeft }: Props) {
  const [members, setMembers] = useState<Member[]>([]);
  const [leaving, setLeaving] = useState(false);
  const [copied, setCopied] = useState(false);
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
      .channel(`room:${room.id}`, { config: { private: true } })
      .on('broadcast', { event: 'members_changed' }, load)
      .subscribe();
    return () => {
      supabase.removeChannel(channel);
    };
  }, [room.id]);

  async function copy() {
    setCopied(await Clipboard.setStringAsync(room.pin));
  }

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
    <>
      <Title subtitle="Share this name and PIN with your group">{room.name}</Title>
      <Card label="PIN" right={<Chip icon="copy-outline" label={copied ? 'Copied' : 'Copy'} onPress={copy} />}>
        <Text style={styles.pin} selectable>
          {room.pin}
        </Text>
      </Card>
      <Card label="Members">
        {members.map((m) => (
          <Body key={m.user_id}>
            {m.display_name}
            {m.user_id === userId ? ' (you)' : ''}
          </Body>
        ))}
      </Card>
      <Button label="Leave group" onPress={leave} disabled={leaving} />
      <ErrorText message={error} />
    </>
  );
}

const styles = StyleSheet.create({
  pin: { ...font.pin, fontVariant: ['tabular-nums'], color: colors.text },
});
