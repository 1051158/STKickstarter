import { Button, Screen, Title } from '../ui';

type Props = { onCreate: () => void; onJoin: () => void; onConnect: () => void };

export default function HomeScreen({ onCreate, onJoin, onConnect }: Props) {
  return (
    <Screen>
      <Title>STKickstarter</Title>
      <Button label="Create a room" onPress={onCreate} primary />
      <Button label="Join a room" onPress={onJoin} />
      <Button label="Connect device" onPress={onConnect} />
    </Screen>
  );
}
