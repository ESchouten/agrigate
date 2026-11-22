# T4C driver

Reads the milking robots a T4C farm management server knows. Link: `http`. The driver is
`src/T4c.cpp`; it only asks, with `GET`, after one login.

| Call | For |
|---|---|
| `POST /t4capi/login/secure` | The login: `data.accessToken.token`, sent on as `Authorization: Bearer`, with the user's `userid`, `herdid`, language and country headers |
| `GET /t4capi/astronaut/overviews` | The robots: `id`, `name`, `barnName` (group), `robotStatus` (raw), `lastProcessTime` (last visit) |
| `GET /t4capi/alarms/astronaut/{id}` | Its alarms: `creationDate`, `indicationId` (code), `description` (text) |
| `GET /t4capi/astronaut/{id}/detail` | Its figures: the first of `astronautKpis` (`totalMilkProduced`, `numberOfMilking`) and `failedMilkingKpi.totalFailedMilkingCount` |

## Not checked on a real server yet

The driver was written from the server's API description and tested against a server that
answers as that description says (`tests/test_t4c.py`). A real server has to show:

- what the numbers of `robotStatus` mean: until then every robot's state is `unknown`, with the
  number in `raw`;
- that the login is taken as `Bearer`, and whether the server also wants the optional
  `licensekey` header or a paired device;
- what an alarm's `type` says about its severity (now `unknown`);
- over which time the figures run, and the unit of the milk.

To record what a server says (logins and passwords are left out of the file):

```bash
cd python && python farm.py t4c <address> --user <name> --record answers.json
```
